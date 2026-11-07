#!/usr/bin/env python3
"""
derive.py - generate Haiku-adapted copies of imported FreeBSD LinuxKPI files.

Each entry below names an imported source, an output file and an ordered list
of (old, new) text replacements. Every 'old' must occur exactly once, so a
FreeBSD update that touches an adapted spot fails loudly instead of being
silently mis-patched. The output keeps the original license header and gets a
note listing what changed. Run after tools/import-freebsd.sh.
"""
import pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
# Sources and outputs are relative to the linuxkpi directory.
LK = ROOT / "haiku/src/libs/compat/linuxkpi"

DERIVED = {
    "common/src/linux_rcu.c": ("haiku/lkpi_rcu.c", """
Haiku changes: (1) ck_epoch_begin/end on a per-CPU record are guarded by a
spinlock with interrupts off, because Haiku's freebsd_network
critical_enter() only pins the thread and does not stop preemption;
(2) linux_synchronize_rcu() waits by sleeping a tick per ck_epoch callback
instead of FreeBSD's sched_bind()/mi_switch() priority dance (Haiku has no
public API to migrate a thread to another CPU or to switch voluntarily
under the thread lock). Readers remain pinned (sched_pin), as in FreeBSD.
""", [
        ("struct linux_epoch_record {\n\tck_epoch_record_t epoch_record;\n",
         "struct linux_epoch_record {\n\tck_epoch_record_t epoch_record;\n\tspinlock lkpi_lock;\t/* Haiku: see header note */\n"),
        ("""	/*
	 * Use a critical section to prevent recursion inside
	 * ck_epoch_begin(). Else this function supports recursion.
	 */
	critical_enter();
	ck_epoch_begin(&record->epoch_record,
	    (ck_epoch_section_t *)&ts->rcu_section[type]);
	TAILQ_INSERT_TAIL(&record->ts_head, ts, rcu_entry[type]);
	critical_exit();""",
         """	{
		/* Haiku: exclude other threads on this CPU (see header). */
		cpu_status lkpi_state = disable_interrupts();
		acquire_spinlock(&record->lkpi_lock);
		ck_epoch_begin(&record->epoch_record,
		    (ck_epoch_section_t *)&ts->rcu_section[type]);
		TAILQ_INSERT_TAIL(&record->ts_head, ts, rcu_entry[type]);
		release_spinlock(&record->lkpi_lock);
		restore_interrupts(lkpi_state);
	}"""),
        ("""	/*
	 * Use a critical section to prevent recursion inside
	 * ck_epoch_end(). Else this function supports recursion.
	 */
	critical_enter();
	ck_epoch_end(&record->epoch_record,
	    (ck_epoch_section_t *)&ts->rcu_section[type]);
	TAILQ_REMOVE(&record->ts_head, ts, rcu_entry[type]);
	critical_exit();""",
         """	{
		cpu_status lkpi_state = disable_interrupts();
		acquire_spinlock(&record->lkpi_lock);
		ck_epoch_end(&record->epoch_record,
		    (ck_epoch_section_t *)&ts->rcu_section[type]);
		TAILQ_REMOVE(&record->ts_head, ts, rcu_entry[type]);
		release_spinlock(&record->lkpi_lock);
		restore_interrupts(lkpi_state);
	}"""),
        ("			record->cpuid = i;\n",
         "			record->cpuid = i;\n			B_INITIALIZE_SPINLOCK(&record->lkpi_lock);\n"),
        ("MPASS(curthread->td_pinned != 0);", "/* td_pinned: not tracked on Haiku */"),
        # synchronize: replace the callback body and the bind/unbind dance
        ("""	struct linux_epoch_record *record =
	    container_of(epoch_record, struct linux_epoch_record, epoch_record);
	struct thread *td = curthread;
	struct task_struct *ts;

	/* check if blocked on the current CPU */
	if (record->cpuid == PCPU_GET(cpuid)) {""",
         """	/* Haiku: a reader still holds this epoch; let it run. Readers can
	 * be on any CPU and may sleep, so just sleep one tick. */
	pause("W", 1);
}

static void __unused
lkpi_freebsd_synchronize_rcu_cb_unused(struct linux_epoch_record *record)
{
	struct thread *td = curthread;
	struct task_struct *ts;

	/* FreeBSD's original strategy, kept for reference, not compiled in. */
	if (0) {"""),
        ("""		if (is_sleeping) {
			thread_unlock(td);
			pause("W", 1);
			thread_lock(td);
		} else {
			/* set new thread priority */
			sched_prio(td, prio);
			/* task switch */
			mi_switch(SW_VOL | SWT_RELINQUISH);""",
         """		if (is_sleeping) {
			pause("W", 1);
		} else {
			(void)prio;"""),
        ("""			 * might happen.
			 */
			thread_lock(td);
		}
	} else {
		/*
		 * To avoid spinning move execution to the other CPU
		 * which is blocking synchronization. Set highest
		 * thread priority so that code gets run. The thread
		 * priority will be restored later.
		 */
		sched_prio(td, 0);
		sched_bind(td, record->cpuid);
	}
}""",
         """			 * might happen.
			 */
		}
	}
	(void)td;
}"""),
        ("""	td = curthread;
	DROP_GIANT();

	/*
	 * Synchronizing RCU might change the CPU core this function
	 * is running on. Save current values:
	 */
	thread_lock(td);

	old_cpu = PCPU_GET(cpuid);
	old_pinned = td->td_pinned;
	old_prio = td->td_priority;
	was_bound = sched_is_bound(td);
	sched_unbind(td);
	td->td_pinned = 0;
	sched_bind(td, old_cpu);

	ck_epoch_synchronize_wait(&linux_epoch[type],
	    &linux_synchronize_rcu_cb, NULL);

	/* restore CPU binding, if any */
	if (was_bound != 0) {
		sched_bind(td, old_cpu);
	} else {
		/* get thread back to initial CPU, if any */
		if (old_pinned != 0)
			sched_bind(td, old_cpu);
		sched_unbind(td);
	}
	/* restore pinned after bind */
	td->td_pinned = old_pinned;

	/* restore thread priority */
	sched_prio(td, old_prio);
	thread_unlock(td);

	PICKUP_GIANT();""",
         """	td = curthread;
	(void)td; (void)was_bound; (void)old_cpu; (void)old_pinned;
	(void)old_prio;
	DROP_GIANT();

	/* Haiku: no CPU binding dance; see header note. */
	ck_epoch_synchronize_wait(&linux_epoch[type],
	    &linux_synchronize_rcu_cb, NULL);

	PICKUP_GIANT();"""),
    ]),
    # The overlay include dir comes first, so this shadows the imported one.
    "common/include/net/mac80211.h": ("haiku/include/net/mac80211.h", """
Haiku change: the net80211 key back-pointer (_k) in struct
ieee80211_key_conf is compiled for Haiku too. Haiku cannot define
__FreeBSD__ (net80211's ieee80211_var.h would then pick the FreeBSD
portability header instead of ieee80211_haiku.h).
""", [
        ("struct ieee80211_key_conf {\n#if defined(__FreeBSD__)\n",
         "struct ieee80211_key_conf {\n#if defined(__FreeBSD__) || defined(__HAIKU__)\n"),
    ]),
}

def main():
    for src, (dst, note, reps) in DERIVED.items():
        text = (LK / src).read_text()
        for old, new in reps:
            n = text.count(old)
            if n != 1:
                sys.exit(f"derive.py: {src}: expected 1 match, found {n}:\n{old[:200]}")
            text = text.replace(old, new)
        end = text.index(" */") + 3
        hdr = (" *\n * ---------------------------------------------------------------------------\n"
               f" * GENERATED by tools/derive.py from FreeBSD\n"
               f" * sys/compat/linuxkpi/{src} (commit: IMPORTED.md).\n"
               + "".join(" * " + l + "\n" if l else " *\n" for l in note.strip().splitlines())
               + " * Do not edit; edit tools/derive.py.\n")
        text = text[:end - 3] + hdr + text[end - 3:]
        (LK / dst).parent.mkdir(parents=True, exist_ok=True)
        (LK / dst).write_text(text)
        print(f"wrote {dst}")

main()
