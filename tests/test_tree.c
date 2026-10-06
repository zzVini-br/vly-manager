#include <unistd.h>

#include "test.h"
#include "vly/snapshot.h"

static pid_t pid_at(const vly_snapshot *snap, int32_t index)
{
    return index == VLY_NO_INDEX ? -1 : snap->procs[index].pid;
}

static const vly_process *find(const vly_snapshot *snap, pid_t pid)
{
    int32_t index = vly_snapshot_index_of(snap, pid);
    return index == VLY_NO_INDEX ? NULL : &snap->procs[index];
}

/* Counts the processes reachable from `first`, giving up past `limit` so a
 * broken tree can't hang the test. */
static size_t count_reachable(const vly_snapshot *snap, int32_t first, size_t limit)
{
    size_t total = 0;
    for (int32_t i = first; i != VLY_NO_INDEX && total <= limit;
         i = snap->procs[i].next_sibling) {
        total += 1 + count_reachable(snap, snap->procs[i].first_child, limit);
    }
    return total;
}

static void test_fixture_tree(void)
{
    vly_snapshot snap;
    vly_snapshot_init(&snap);
    CHECK(vly_snapshot_collect(&snap, VLY_FIXTURES_DIR "/proc") == 0);
    vly_snapshot_build_tree(&snap);

    /* Roots: systemd (1) and kthreadd (2), in pid order. */
    CHECK(pid_at(&snap, snap.first_root) == 1);
    const vly_process *systemd = find(&snap, 1);
    const vly_process *kthreadd = find(&snap, 2);
    const vly_process *steam = find(&snap, 100);
    const vly_process *zombie = find(&snap, 101);
    const vly_process *reaper = find(&snap, 1000);
    CHECK(systemd && kthreadd && steam && zombie && reaper);
    if (!(systemd && kthreadd && steam && zombie && reaper)) {
        vly_snapshot_free(&snap);
        return;
    }

    CHECK(pid_at(&snap, systemd->next_sibling) == 2);
    CHECK(kthreadd->next_sibling == VLY_NO_INDEX);
    CHECK(systemd->parent == VLY_NO_INDEX);

    CHECK(pid_at(&snap, kthreadd->first_child) == 50);
    CHECK(pid_at(&snap, systemd->first_child) == 100);
    CHECK(pid_at(&snap, steam->parent) == 1);

    /* steam's children: the zombie and reaper, in pid order. */
    CHECK(pid_at(&snap, steam->first_child) == 101);
    CHECK(pid_at(&snap, zombie->next_sibling) == 1000);
    CHECK(reaper->next_sibling == VLY_NO_INDEX);
    CHECK(reaper->first_child == VLY_NO_INDEX);

    CHECK(count_reachable(&snap, snap.first_root, snap.count) == snap.count);

    /* Rebuilding must not duplicate links. */
    vly_snapshot_build_tree(&snap);
    CHECK(count_reachable(&snap, snap.first_root, snap.count) == snap.count);

    vly_snapshot_free(&snap);
}

static void test_live_tree(void)
{
    vly_snapshot snap;
    vly_snapshot_init(&snap);
    CHECK(vly_snapshot_collect(&snap, "/proc") == 0);
    vly_snapshot_build_tree(&snap);

    CHECK(count_reachable(&snap, snap.first_root, snap.count) == snap.count);

    const vly_process *self = find(&snap, getpid());
    CHECK(self != NULL);
    if (self != NULL) {
        CHECK(pid_at(&snap, self->parent) == getppid());
    }

    vly_snapshot_free(&snap);
}

int main(void)
{
    test_fixture_tree();
    test_live_tree();
    return TEST_RESULT();
}
