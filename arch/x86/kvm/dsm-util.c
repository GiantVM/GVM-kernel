// SPDX-License-Identifier: GPL-2.0
/*
 * Support KVM software distributed memory (Ivy Protocol)
 *
 * This feature allows us to run multiple KVM instances on different machines
 * sharing the same address space.
 *
 * Copyright (C) 2019-2026, Trusted Cloud Group, Institute of Scalable Computing,
 * Shanghai Jiao Tong University.
 *
 * Authors:
 *   Chen Yubin <binsschen@sjtu.edu.cn>
 *   Ding Zhuocheng <tcbbd@sjtu.edu.cn>
 *   Zhang Jin <jzhang3002@sjtu.edu.cn>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include <linux/kvm_host.h>
#include <linux/kvm.h>
#include "mmu.h"
#include "dsm.h"
#include "dsm-util.h"

#include <linux/kthread.h>
#include <linux/mmu_context.h>

struct kvm_network_ops network_ops;

int get_dsm_address(struct kvm *kvm, int dsm_id, struct dsm_address *addr)
{
	if (!addr)
		return -EINVAL;
	addr->port = 37710 + dsm_id;
	addr->host = kvm->arch.cluster_iplist[dsm_id];

	return 0;
}

int dsm_create_memslot(struct kvm_dsm_memory_slot *slot, unsigned long npages)
{
	unsigned long i;
	int ret = 0;

	slot->vfn_dsm_state = kvzalloc(array_size(npages,
					   sizeof(*slot->vfn_dsm_state)),
				GFP_KERNEL);
	if (!slot->vfn_dsm_state)
		return -ENOMEM;

	slot->rmap = kvzalloc(array_size(npages,
					   sizeof(*slot->rmap)),
				GFP_KERNEL);
	if (!slot->rmap) {
		ret = -ENOMEM;
		goto out_free_dsm_state;
	}

	slot->backup_rmap =
		kvzalloc(npages * sizeof(*slot->backup_rmap), GFP_KERNEL);
	if (!slot->backup_rmap) {
		ret = -ENOMEM;
		goto out_free_rmap;
	}

	slot->rmap_lock = kmalloc(sizeof(*slot->rmap_lock), GFP_KERNEL);
	if (!slot->rmap_lock) {
		ret = -ENOMEM;
		goto out_free_backup_rmap;
	}
	mutex_init(slot->rmap_lock);

	for (i = 0; i < npages; i++) {
		mutex_init(&slot->vfn_dsm_state[i].fast_path_lock);
		mutex_init(&slot->vfn_dsm_state[i].lock);
	}

	return ret;

out_free_backup_rmap:
	kvfree(slot->backup_rmap);
out_free_rmap:
	kvfree(slot->rmap);
out_free_dsm_state:
	kvfree(slot->vfn_dsm_state);
	return ret;
}

int insert_hvaslot(struct kvm_dsm_memslots *slots, int pos, hfn_t start,
		   unsigned long npages)
{
	int ret, i;

	if (slots->used_slots == KVM_MEM_SLOTS_NUM) {
		pr_err("kvm-dsm: all slots are used, no more space for new hvaslot[%llu,%lu]\n",
		       start, npages);
		return -EINVAL;
	}

	for (i = slots->used_slots++; i > pos; i--)
		slots->memslots[i] = slots->memslots[i - 1];

	slots->memslots[i].base_vfn = start;
	slots->memslots[i].npages = npages;
	pr_devel("kvm-dsm: create new hvaslot[%llu,%lu]\n", start, npages);
	ret = dsm_create_memslot(&slots->memslots[i], npages);
	if (ret < 0)
		return ret;

	return 0;
}

void dsm_lock(struct kvm *kvm, struct kvm_dsm_memory_slot *slot, hfn_t vfn,
	      struct kvm_memory_slot *memslot)
{
	return mutex_lock(&slot->vfn_dsm_state[vfn - slot->base_vfn].lock);
}

void dsm_unlock(struct kvm *kvm, struct kvm_dsm_memory_slot *slot, hfn_t vfn,
		struct kvm_memory_slot *memslot)
{
	gfn_t gfn = vfn - slot->base_vfn;

	if (memslot)
		gfn += memslot->base_gfn;
	return mutex_unlock(&slot->vfn_dsm_state[gfn].lock);
}

static int __kvm_dsm_trylock(struct mutex *l)
{
	int retry_cnt = 0;

	while (!mutex_trylock(l)) {
		retry_cnt++;
		if (retry_cnt > 1024)
			return -EAGAIN;
	}
	return 1;
}

int dsm_trylock(struct kvm *kvm, struct kvm_dsm_memory_slot *slot, hfn_t vfn)
{
	return __kvm_dsm_trylock(&slot->vfn_dsm_state[vfn - slot->base_vfn].lock);
}

int dsm_trylock_timeout(struct kvm *kvm, struct kvm_dsm_memory_slot *slot,
			hfn_t vfn, int *retry_cnt,
			struct kvm_memory_slot *memslot)
{
	return dsm_trylock(kvm, slot, vfn);
}

int dsm_encode_diff(struct kvm_dsm_memory_slot *slot, hfn_t vfn, int msg_sender,
		    char *page, struct kvm_memory_slot *memslot, gfn_t gfn,
		    u16 version)
{
	int length = PAGE_SIZE;

#ifdef KVM_DSM_W_SHARED
	/*
	 * The same versions denote there is no need to fetch page (an ack is still
	 * necessary).
	 *
	 * FIXME: At the initialization period, version of pages in kvm 0 should be
	 * 1. However, due to the complexity of hvaslot initialization
	 * (insert/remove, backup balabala...), it's not implemented yet.
	 */
	if (version >= 20 && version == dsm_get_version(slot, vfn))
		return 0;
#endif

	return length;
}

void dsm_decode_diff(char *page, int resp_len, struct kvm_memory_slot *memslot,
		     gfn_t gfn)
{
	if (WARN_ON_ONCE(resp_len != 0 && resp_len != PAGE_SIZE))
		return;
}

void dsm_set_twin_conditionally(struct kvm_dsm_memory_slot *slot, hfn_t vfn,
				char *page, struct kvm_memory_slot *memslot,
				gfn_t gfn, bool is_owner, version_t version)
{
}

int kvm_dsm_connect(struct kvm *kvm, int dest_id, kconnection_t **conn_sock)
{
	int ret;
	struct dsm_address addr;

	ret = get_dsm_address(kvm, dest_id, &addr);
	if (ret < 0) {
		pr_err("kvm-dsm: address not configured properly for node-%d\n",
		       dest_id);
		return ret;
	}

	ret = network_ops.connect(addr.host, addr.port, conn_sock);
	if (ret < 0) {
		pr_err("kvm-dsm: node-%d failed to connect to node-%d\n",
		       kvm->arch.dsm_id, dest_id);
		return ret;
	}
	pr_info("kvm-dsm: node-%d established connection with node-%d [%s:%u]\n",
		kvm->arch.dsm_id, dest_id, addr.host, addr.port);
	return 0;
}

int kvm_read_guest_page_nonlocal(struct kvm *kvm, struct kvm_memory_slot *slot,
				 gfn_t gfn, void *data, int offset, int len)
{
	int ret = 0;

	kthread_use_mm(kvm->mm);
	ret = kvm_read_guest_page(kvm, gfn, data, offset, len);
	kthread_unuse_mm(kvm->mm);
	return ret;
}

int kvm_write_guest_page_nonlocal(struct kvm *kvm, struct kvm_memory_slot *slot,
				  gfn_t gfn, const void *data, int offset,
				  int len)
{
	int ret = 0;

	kthread_use_mm(kvm->mm);
	ret = kvm_write_guest_page(kvm, gfn, data, offset, len);
	kthread_unuse_mm(kvm->mm);
	return ret;
}
