/*
 * Copyright 2026, LinuxKPI_HaikuOS contributors.
 * Distributed under the terms of the MIT License.
 *
 * Per-thread state for LinuxKPI on Haiku:
 *  - struct thread / curthread (include/sys/proc.h): Haiku's freebsd_network
 *    has none; LinuxKPI hangs Linux 'current' off curthread->td_lkpi_task.
 *  - FreeBSD sleepqueue(9) (include/sys/sleepqueue.h), used by LinuxKPI's
 *    wait queues, completions and schedule(): implemented with one Haiku
 *    ConditionVariable per sleeping thread, on hashed wait channels.
 *  - sched_pin()/sched_unpin(): Haiku thread_pin_to_current_cpu().
 *
 * Built without the LinuxKPI preinclude ("shim" profile): E* values are
 * Haiku's; results returned to LinuxKPI go through lkpi_errno_from_haiku().
 */

/* Haiku kernel headers first: freebsd_network's haiku-module.h (pulled in
 * by <sys/param.h>) #undefs ASSERT, which these inline functions use. */
#include <KernelExport.h>
#include <OS.h>
#include <condition_variable.h>
#include <interrupts.h>
#include <smp.h>
#include <thread.h>
#include <util/AutoLock.h>

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/sleepqueue.h>
#include <sys/kernel.h>

#include <new>
#include <string.h>

extern "C" int lkpi_errno_from_haiku(int haikuError);

namespace {

struct lkpi_thread_rec;

struct sleepq_chain {
	spinlock		lock;
	lkpi_thread_rec*	waiters;	/* singly linked, LIFO */
};

struct lkpi_thread_rec {
	struct thread		td;		/* must be first */
	lkpi_thread_rec*	hashNext;

	/* sleepqueue state; a thread waits on at most one channel */
	const void*		sqWchan;
	lkpi_thread_rec*	sqNext;
	bool			sqWoken;
	bigtime_t		sqTimeout;
	ConditionVariable	sqVar;
	ConditionVariableEntry	sqEntry;

	/* the chain lock this thread holds via sleepq_lock() */
	sleepq_chain*		sqHeld;
	cpu_status		sqIrqState;
};

const int kThreadHashSize = 256;
const int kChainCount = 128;

spinlock sThreadHashLock = B_SPINLOCK_INITIALIZER;
lkpi_thread_rec* sThreadHash[kThreadHashSize];
sleepq_chain sChains[kChainCount];
struct proc sProc0 = { 0, "kernel" };

inline sleepq_chain*
chain_for(const void* wchan)
{
	return &sChains[((uintptr_t)wchan >> 6) % kChainCount];
}

lkpi_thread_rec*
rec_lookup(thread_id id)
{
	InterruptsSpinLocker locker(sThreadHashLock);
	for (lkpi_thread_rec* rec = sThreadHash[id % kThreadHashSize];
			rec != NULL; rec = rec->hashNext) {
		if (rec->td.td_tid == id)
			return rec;
	}
	return NULL;
}

}	// namespace


extern "C" struct thread*
lkpi_curthread(void)
{
	thread_id id = find_thread(NULL);
	lkpi_thread_rec* rec = rec_lookup(id);
	if (rec != NULL)
		return &rec->td;

	/* First LinuxKPI use on this thread: allocate outside the lock. */
	rec = new(std::nothrow) lkpi_thread_rec;
	if (rec == NULL)
		panic("LinuxKPI: out of memory allocating struct thread");
	memset(&rec->td, 0, sizeof(rec->td));
	rec->td.td_tid = id;
	rec->td.td_proc = &sProc0;
	rec->td.td_priority = thread_get_current_thread()->priority;
	strlcpy(rec->td.td_name, thread_get_current_thread()->name,
		sizeof(rec->td.td_name));
	rec->sqWchan = NULL;
	rec->sqNext = NULL;
	rec->sqWoken = false;
	rec->sqTimeout = 0;
	rec->sqHeld = NULL;

	InterruptsSpinLocker locker(sThreadHashLock);
	rec->hashNext = sThreadHash[id % kThreadHashSize];
	sThreadHash[id % kThreadHashSize] = rec;
	return &rec->td;
	/* Records are never freed yet: see docs/STATUS.md, "thread records". */
}


extern "C" void
sched_pin(void)
{
	thread_pin_to_current_cpu(thread_get_current_thread());
}


extern "C" void
sched_unpin(void)
{
	thread_unpin_from_current_cpu(thread_get_current_thread());
}


extern "C" void
sched_relinquish(struct thread*)
{
	thread_yield();
}


extern "C" void
kern_yield(int)
{
	thread_yield();
}


extern "C" int
td_ast_pending(struct thread*, int)
{
	return 0;
}


//	#pragma mark - sleepqueue(9)


static inline lkpi_thread_rec*
cur_rec()
{
	return (lkpi_thread_rec*)lkpi_curthread();
}


extern "C" void
sleepq_lock(const void* wchan)
{
	lkpi_thread_rec* self = cur_rec();
	sleepq_chain* chain = chain_for(wchan);

	cpu_status state = disable_interrupts();
	acquire_spinlock(&chain->lock);
	self->sqHeld = chain;
	self->sqIrqState = state;
}


extern "C" void
sleepq_release(const void* wchan)
{
	lkpi_thread_rec* self = cur_rec();
	sleepq_chain* chain = self->sqHeld;

	self->sqHeld = NULL;
	release_spinlock(&chain->lock);
	restore_interrupts(self->sqIrqState);
}


extern "C" void
sleepq_add(const void* wchan, struct lock_object*, const char* wmesg,
	int flags, int queue)
{
	lkpi_thread_rec* self = cur_rec();
	sleepq_chain* chain = self->sqHeld;

	self->sqWchan = wchan;
	self->sqWoken = false;
	self->sqTimeout = 0;
	self->sqVar.Init(self, wmesg != NULL ? wmesg : "lkpi sleepq");
	self->sqVar.Add(&self->sqEntry);
	self->sqNext = chain->waiters;
	chain->waiters = self;
}


extern "C" void
sleepq_set_timeout(const void* wchan, int timo)
{
	/* timo is in ticks (hz per second) */
	cur_rec()->sqTimeout = (bigtime_t)timo * 1000000 / hz;
}


static void
chain_remove(sleepq_chain* chain, lkpi_thread_rec* rec)
{
	for (lkpi_thread_rec** p = &chain->waiters; *p != NULL;
			p = &(*p)->sqNext) {
		if (*p == rec) {
			*p = rec->sqNext;
			rec->sqNext = NULL;
			return;
		}
	}
}


/* Drop the chain lock, sleep, and return a FreeBSD errno (0, EINTR,
 * EWOULDBLOCK). FreeBSD returns with the chain unlocked too. */
static int
sleepq_do_wait(bool interruptible, bool timed)
{
	lkpi_thread_rec* self = cur_rec();
	sleepq_chain* chain = self->sqHeld;
	uint32 flags = interruptible ? B_CAN_INTERRUPT : 0;
	bigtime_t timeout = 0;

	if (timed) {
		flags |= B_RELATIVE_TIMEOUT;
		timeout = self->sqTimeout;
	}

	self->sqHeld = NULL;
	release_spinlock(&chain->lock);
	restore_interrupts(self->sqIrqState);

	status_t status = self->sqEntry.Wait(flags, timeout);

	cpu_status state = disable_interrupts();
	acquire_spinlock(&chain->lock);
	bool woken = self->sqWoken;
	if (!woken)
		chain_remove(chain, self);
	self->sqWchan = NULL;
	release_spinlock(&chain->lock);
	restore_interrupts(state);

	if (woken || status == B_OK)
		return 0;
	if (status == B_TIMED_OUT || status == B_WOULD_BLOCK)
		return lkpi_errno_from_haiku(EWOULDBLOCK);
	return lkpi_errno_from_haiku(EINTR);
}


extern "C" void
sleepq_wait(const void*, int)
{
	sleepq_do_wait(false, false);
}


extern "C" int
sleepq_wait_sig(const void*, int)
{
	return sleepq_do_wait(true, false);
}


extern "C" int
sleepq_timedwait(const void*, int)
{
	return sleepq_do_wait(false, true);
}


extern "C" int
sleepq_timedwait_sig(const void*, int)
{
	return sleepq_do_wait(true, true);
}


/* Caller holds the chain lock (sleepq_lock()); FreeBSD semantics. */
static int
sleepq_wake(const void* wchan, bool all)
{
	sleepq_chain* chain = chain_for(wchan);
	lkpi_thread_rec** p = &chain->waiters;

	while (*p != NULL) {
		lkpi_thread_rec* rec = *p;
		if (rec->sqWchan != wchan || rec->sqWoken) {
			p = &rec->sqNext;
			continue;
		}
		*p = rec->sqNext;
		rec->sqNext = NULL;
		rec->sqWoken = true;
		rec->sqVar.NotifyOne();
		if (!all)
			break;
	}
	return 0;	/* FreeBSD: "need to kick swapper" — never on Haiku */
}


extern "C" int
sleepq_signal(const void* wchan, int flags, int pri, int queue)
{
	return sleepq_wake(wchan, false);
}


extern "C" int
sleepq_broadcast(const void* wchan, int flags, int pri, int queue)
{
	return sleepq_wake(wchan, true);
}


extern "C" void
lkpi_sleepq_init(void)
{
	for (int i = 0; i < kChainCount; i++) {
		B_INITIALIZE_SPINLOCK(&sChains[i].lock);
		sChains[i].waiters = NULL;
	}
}


extern "C" void
tdsignal(struct thread* td, int sig)
{
	send_signal_etc(td->td_tid, sig, B_DO_NOT_RESCHEDULE);
}


//	#pragma mark - process model (include/sys/proc.h)


extern "C" struct proc*
lkpi_proc0(void)
{
	return &sProc0;
}


/* Iteration is lock-free: records are only ever prepended to a hash chain
 * (next pointer written before publication) and never freed. */
static struct thread*
first_from_bucket(int bucket)
{
	for (; bucket < kThreadHashSize; bucket++) {
		lkpi_thread_rec* rec = __atomic_load_n(&sThreadHash[bucket], __ATOMIC_ACQUIRE);
		if (rec != NULL)
			return &rec->td;
	}
	return NULL;
}


extern "C" struct thread*
lkpi_thread_first(struct proc*)
{
	return first_from_bucket(0);
}


extern "C" struct thread*
lkpi_thread_next(struct thread* td)
{
	lkpi_thread_rec* rec = (lkpi_thread_rec*)td;
	if (rec->hashNext != NULL)
		return &rec->hashNext->td;
	return first_from_bucket(rec->td.td_tid % kThreadHashSize + 1);
}


extern "C" struct thread*
tdfind(int32_t tid, pid_t)
{
	lkpi_thread_rec* rec = rec_lookup(tid);
	return rec != NULL ? &rec->td : NULL;
}


extern "C" struct proc*
pfind(pid_t)
{
	return NULL;
}


extern "C" void
thread_reap_barrier(void)
{
}


extern "C" bool
lkpi_can_sleep(void)
{
	return are_interrupts_enabled();
}
