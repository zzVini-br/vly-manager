#include "vly/snapshot.h"

void vly_snapshot_build_tree(vly_snapshot *snap)
{
    snap->first_root = VLY_NO_INDEX;
    for (size_t i = 0; i < snap->count; i++) {
        snap->procs[i].first_child = VLY_NO_INDEX;
    }

    /* Walking backwards and prepending keeps each sibling list in pid order. */
    for (size_t i = snap->count; i-- > 0;) {
        vly_process *proc = &snap->procs[i];
        int32_t self = (int32_t)i;
        int32_t parent = vly_snapshot_index_of(snap, proc->ppid);

        if (parent == self) {
            parent = VLY_NO_INDEX;
        }

        int32_t *head = parent == VLY_NO_INDEX ? &snap->first_root
                                               : &snap->procs[parent].first_child;
        proc->parent = parent;
        proc->next_sibling = *head;
        *head = self;
    }
}
