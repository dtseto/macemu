/*
 *  timer.cpp - Time Manager emulation
 *
 *  Basilisk II (C) 1997-2008 Christian Bauer and Marc Hellwig
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "sysdeps.h"
#include "timer.h"
#include "macos_util.h"
#include "main.h"
#include "cpu_emulation.h"

#ifdef PRECISE_TIMING_POSIX
#include <pthread.h>
#include <errno.h>
#endif

#ifdef PRECISE_TIMING_MACH
#include <mach/mach.h>
#endif

#define DEBUG 0
#include "debug.h"


#define TM_QUEUE 0			// Enable TMQueue management (doesn't work)


// Definitions for Time Manager
enum {	// TMTask struct
	tmAddr = 6,
	tmCount = 10,
	tmWakeUp = 14,
	tmReserved = 18
};


// Array of additional info for each installed TMTask
struct TMDesc {
	uint32 task;		// Mac address of associated TMTask
	tm_time_t wakeup;	// Time this task is scheduled for execution
	TMDesc *next;
};

static TMDesc *tmDescList;

#if PRECISE_TIMING
#ifdef PRECISE_TIMING_BEOS
static thread_id timer_thread = -1;
static bool thread_active = true;
static const tm_time_t wakeup_time_max = 0x7fffffffffffffff; 
static volatile tm_time_t wakeup_time = wakeup_time_max;
static sem_id wakeup_time_sem = -1;
static int32 timer_func(void *arg);
#endif
#ifdef PRECISE_TIMING_POSIX
static pthread_t timer_thread;
static bool timer_thread_active = false;
static volatile bool timer_thread_cancel = false;
static tm_time_t wakeup_time_max = { 0x7fffffff, 999999999 };
static tm_time_t wakeup_time = wakeup_time_max;
static pthread_mutex_t wakeup_time_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wakeup_time_cond;
static bool wakeup_time_cond_initialized = false;
static void *timer_func(void *arg);
#endif
#ifdef PRECISE_TIMING_MACH
static clock_serv_t system_clock;
static thread_act_t timer_thread;
static bool timer_thread_active = false;
static tm_time_t wakeup_time_max = { 0x7fffffff, 999999999 };
static tm_time_t wakeup_time = wakeup_time_max;
static semaphore_t wakeup_time_sem;
static void *timer_func(void *arg);
#endif
#endif


inline static void free_desc(TMDesc *desc)
{
	if (desc == tmDescList) {
		tmDescList = desc->next;
	} else {
		for (TMDesc *d = tmDescList; d; d = d->next) {
			if (d->next == desc) {
				d->next = desc->next;
				break;
			}
		}
	}
	delete desc;
}

/*
 *  Find descriptor associated with given TMTask
 */

inline static TMDesc *find_desc(uint32 tm)
{
	TMDesc *desc = tmDescList;
	while (desc) {
		if (desc->task == tm) {
			return desc;
		}
		desc = desc->next;
	}
	return NULL;
}


/*
 *  Enqueue task in Time Manager queue
 */

static void enqueue_tm(uint32 tm)
{
#if TM_QUEUE
	uint32 tm_var = ReadMacInt32(0xb30);
	WriteMacInt32(tm + qLink, ReadMacInt32(tm_var));
	WriteMacInt32(tm_var, tm);
#endif
}


/*
 *  Remove task from Time Manager queue
 */

static void dequeue_tm(uint32 tm)
{
#if TM_QUEUE
	uint32 p = ReadMacInt32(0xb30);
	while (p) {
		uint32 next = ReadMacInt32(p + qLink);
		if (next == tm) {
			WriteMacInt32(p + qLink, ReadMacInt32(next + qLink));
			return;
		}
	}
#endif
}


/*
 *  Timer thread operations
 */

#ifdef PRECISE_TIMING_POSIX
// Initialize timer thread
static bool timer_thread_init(void)
{
	pthread_condattr_t cond_attr;
	if (pthread_condattr_init(&cond_attr) != 0)
		return false;
#if defined(CLOCK_MONOTONIC) && defined(HAVE_PTHREAD_CONDATTR_SETCLOCK)
	if (pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC) != 0) {
		pthread_condattr_destroy(&cond_attr);
		return false;
	}
#endif
	if (pthread_cond_init(&wakeup_time_cond, &cond_attr) != 0) {
		pthread_condattr_destroy(&cond_attr);
		return false;
	}
	pthread_condattr_destroy(&cond_attr);
	wakeup_time_cond_initialized = true;
	timer_thread_cancel = false;
	if (pthread_create(&timer_thread, NULL, timer_func, NULL) != 0) {
		wakeup_time_cond_initialized = false;
		pthread_cond_destroy(&wakeup_time_cond);
		return false;
	}
	return true;
}

// Kill timer thread
static void timer_thread_kill(void)
{
	pthread_mutex_lock(&wakeup_time_lock);
	timer_thread_cancel = true;
	if (wakeup_time_cond_initialized)
		pthread_cond_broadcast(&wakeup_time_cond);
	pthread_mutex_unlock(&wakeup_time_lock);
	pthread_join(timer_thread, NULL);
	if (wakeup_time_cond_initialized) {
		pthread_cond_destroy(&wakeup_time_cond);
		wakeup_time_cond_initialized = false;
	}
}
#endif


/*
 *  Initialize Time Manager
 */

void TimerInit(void)
{
	TimerReset();

#if PRECISE_TIMING_MACH
	printf("B2_OPT path=monotonic_timer active\n");
#elif PRECISE_TIMING_POSIX
#if defined(CLOCK_MONOTONIC)
	printf("B2_OPT path=monotonic_timer active\n");
#else
	printf("B2_OPT path=monotonic_timer fallback reason=clock_monotonic_unavailable\n");
#endif
#elif PRECISE_TIMING
	printf("B2_OPT path=monotonic_timer fallback reason=timer_backend_not_monotonic\n");
#else
	printf("B2_OPT path=monotonic_timer fallback reason=precise_timing_unavailable\n");
#endif

#if PRECISE_TIMING
	// Start timer thread
#ifdef PRECISE_TIMING_BEOS
	wakeup_time_sem = create_sem(1, "Wakeup Time");
	timer_thread = spawn_thread(timer_func, "Time Manager", B_REAL_TIME_PRIORITY, NULL);
	resume_thread(timer_thread);
#elif PRECISE_TIMING_MACH
	pthread_t pthread;
	
	// Keep the sleep clock in the same monotonic clock domain used by
	// timer_current_time() in timer_unix.cpp.
	host_get_clock_service(mach_host_self(), SYSTEM_CLOCK, &system_clock);
	semaphore_create(mach_task_self(), &wakeup_time_sem, SYNC_POLICY_FIFO, 1);

	pthread_create(&pthread, NULL, &timer_func, NULL);
#endif
#ifdef PRECISE_TIMING_POSIX
	timer_thread_active = timer_thread_init();
#endif
#endif
}


/*
 *  Exit Time Manager
 */

void TimerExit(void)
{
#if PRECISE_TIMING
	// Quit timer thread
	if (timer_thread_active) {
#ifdef PRECISE_TIMING_BEOS
		status_t l;
		thread_active = false;
		suspend_thread(timer_thread);
		resume_thread(timer_thread);
		wait_for_thread(timer_thread, &l);
		delete_sem(wakeup_time_sem);
#endif
#ifdef PRECISE_TIMING_MACH
		timer_thread_active = false;
		semaphore_destroy(mach_task_self(), wakeup_time_sem);
#endif
#ifdef PRECISE_TIMING_POSIX
		timer_thread_kill();
#endif
	}
#endif
}


/*
 *  Emulator reset, remove all timer tasks
 */

void TimerReset(void)
{
	TMDesc *desc = tmDescList;
	while (desc) {
		TMDesc *next = desc->next;
		delete desc;
		desc = next;
	}
	tmDescList = NULL;
}


/*
 *  Insert timer task
 */

int16 InsTime(uint32 tm, uint16 trap)
{
	D(bug("InsTime %08lx, trap %04x\n", tm, trap));
	WriteMacInt16(tm + qType, (ReadMacInt16(tm + qType) & 0x1fff) | ((trap << 4) & 0x6000));
	if (find_desc(tm))
		printf("WARNING: InsTime(%08x): Task re-inserted\n", tm);
	else {
		TMDesc *desc = new TMDesc;
		desc->task = tm;
		desc->next = tmDescList;
		tmDescList = desc;
	}
	return 0;
}


/*
 *  Remove timer task
 */

int16 RmvTime(uint32 tm)
{
	D(bug("RmvTime %08lx\n", tm));

	// Find descriptor
	TMDesc *desc = find_desc(tm);
	if (!desc) {
		printf("WARNING: RmvTime(%08x): Descriptor not found\n", tm);
		return 0;
	}

	// Task active?
#if PRECISE_TIMING_BEOS
	while (acquire_sem(wakeup_time_sem) == B_INTERRUPTED) ;
	suspend_thread(timer_thread);
#endif
#ifdef PRECISE_TIMING_MACH
	semaphore_wait(wakeup_time_sem);
	thread_suspend(timer_thread);
#endif
#if PRECISE_TIMING_POSIX
	pthread_mutex_lock(&wakeup_time_lock);
#endif
	if (ReadMacInt16(tm + qType) & 0x8000) {

		// Yes, make task inactive and remove it from the Time Manager queue
		WriteMacInt16(tm + qType, ReadMacInt16(tm + qType) & 0x7fff);
		dequeue_tm(tm);
#if PRECISE_TIMING
		// Look for next task to be called and set wakeup_time
		wakeup_time = wakeup_time_max;
		for (TMDesc *d = tmDescList; d; d = d->next)
			if ((ReadMacInt16(d->task + qType) & 0x8000))
				if (timer_cmp_time(d->wakeup, wakeup_time) < 0)
					wakeup_time = d->wakeup;
#endif

		// Compute remaining time
		tm_time_t remaining, current;
		timer_current_time(current);
		timer_sub_time(remaining, desc->wakeup, current);
		WriteMacInt32(tm + tmCount, timer_host2mac_time(remaining));
	} else
		WriteMacInt32(tm + tmCount, 0);
	D(bug(" tmCount %d\n", ReadMacInt32(tm + tmCount)));
#if PRECISE_TIMING_BEOS
	release_sem(wakeup_time_sem);
	thread_info info;
	do {
		resume_thread(timer_thread);			// This will unblock the thread
		get_thread_info(timer_thread, &info);
	} while (info.state == B_THREAD_SUSPENDED);	// Sometimes, resume_thread() doesn't work (BeOS bug?)
#endif
#ifdef PRECISE_TIMING_MACH
	semaphore_signal(wakeup_time_sem);
	thread_abort(timer_thread);
	thread_resume(timer_thread);
#endif
#if PRECISE_TIMING_POSIX
	pthread_mutex_unlock(&wakeup_time_lock);
	if (wakeup_time_cond_initialized)
		pthread_cond_signal(&wakeup_time_cond);
#endif

	// Free descriptor
	free_desc(desc);
	return 0;
}


/*
 *  Start timer task
 */

int16 PrimeTime(uint32 tm, int32 time)
{
	D(bug("PrimeTime %08x, time %d\n", tm, time));

	// Find descriptor
	TMDesc *desc = find_desc(tm);
	if (!desc) {
		printf("FATAL: PrimeTime(%08x): Descriptor not found\n", tm);
		return 0;
	}

	// Convert delay time
	tm_time_t delay;
	timer_mac2host_time(delay, time);

	// Extended task?
	if (ReadMacInt16(tm + qType) & 0x4000) {

		// Yes, tmWakeUp set?
		if (ReadMacInt32(tm + tmWakeUp)) {

			// PrimeTime(0) can either mean (a) "the task runs as soon as interrupts are enabled"
			// or (b) "continue previous delay" if an expired task was stopped via RmvTime() and
			// then re-installed using InsXTime(). Since tmWakeUp was set, this is case (b).
			// The remaining time was saved in tmCount by RmvTime().
			if (time == 0) {
				timer_mac2host_time(delay, ReadMacInt16(tm + tmCount));
			}

			// Yes, calculate wakeup time relative to last scheduled time
			tm_time_t wakeup;
			timer_add_time(wakeup, desc->wakeup, delay);
			desc->wakeup = wakeup;

		} else {

			// No, calculate wakeup time relative to current time
			tm_time_t now;
			timer_current_time(now);
			timer_add_time(desc->wakeup, now, delay);
		}

		// Set tmWakeUp to indicate that task was scheduled
		WriteMacInt32(tm + tmWakeUp, 0x12345678);

	} else {

		// Not extended task, calculate wakeup time relative to current time
		tm_time_t now;
		timer_current_time(now);
		timer_add_time(desc->wakeup, now, delay);
	}

	// Make task active and enqueue it in the Time Manager queue
#if PRECISE_TIMING_BEOS
	while (acquire_sem(wakeup_time_sem) == B_INTERRUPTED) ;
	suspend_thread(timer_thread);
#endif
#ifdef PRECISE_TIMING_MACH
	semaphore_wait(wakeup_time_sem);
	thread_suspend(timer_thread);
#endif
#if PRECISE_TIMING_POSIX
	pthread_mutex_lock(&wakeup_time_lock);
#endif
	WriteMacInt16(tm + qType, ReadMacInt16(tm + qType) | 0x8000);
	enqueue_tm(tm);
#if PRECISE_TIMING
	// Look for next task to be called and set wakeup_time
	wakeup_time = wakeup_time_max;
	for (TMDesc *d = tmDescList; d; d = d->next)
		if ((ReadMacInt16(d->task + qType) & 0x8000))
			if (timer_cmp_time(d->wakeup, wakeup_time) < 0)
				wakeup_time = d->wakeup;
#ifdef PRECISE_TIMING_BEOS
	release_sem(wakeup_time_sem);
	thread_info info;
	do {
		resume_thread(timer_thread);			// This will unblock the thread
		get_thread_info(timer_thread, &info);
	} while (info.state == B_THREAD_SUSPENDED);	// Sometimes, resume_thread() doesn't work (BeOS bug?)
#endif
#ifdef PRECISE_TIMING_MACH
	semaphore_signal(wakeup_time_sem);
	thread_abort(timer_thread);
	thread_resume(timer_thread);
#endif
#ifdef PRECISE_TIMING_POSIX
	pthread_mutex_unlock(&wakeup_time_lock);
	if (wakeup_time_cond_initialized)
		pthread_cond_signal(&wakeup_time_cond);
#endif
#endif
	return 0;
}


/*
 *  Time Manager thread
 */

#ifdef PRECISE_TIMING_BEOS
static int32 timer_func(void *arg)
{
	while (thread_active) {

		// Wait until time specified by wakeup_time
		snooze_until(wakeup_time, B_SYSTEM_TIMEBASE);

		while (acquire_sem(wakeup_time_sem) == B_INTERRUPTED) ;
		if (wakeup_time < system_time()) {

			// Timer expired, trigger interrupt
			wakeup_time = 0x7fffffffffffffff;
			SetInterruptFlag(INTFLAG_TIMER);
			TriggerInterrupt();
		}
		release_sem(wakeup_time_sem);
	}
	return 0;
}
#endif

#ifdef PRECISE_TIMING_MACH
static void *timer_func(void *arg)
{
	timer_thread = mach_thread_self();
	timer_thread_active = true;
	
	while (timer_thread_active) {
		clock_sleep(system_clock, TIME_ABSOLUTE, wakeup_time, NULL);
		semaphore_wait(wakeup_time_sem);
	   
		tm_time_t system_time;
		
		timer_current_time(system_time);
		if (timer_cmp_time(wakeup_time, system_time) < 0) {
			wakeup_time = wakeup_time_max;
			SetInterruptFlag(INTFLAG_TIMER);
			TriggerInterrupt();
		}
		semaphore_signal(wakeup_time_sem);
	}
	return NULL;
}
#endif

#ifdef PRECISE_TIMING_POSIX
static void *timer_func(void *arg)
{
	pthread_mutex_lock(&wakeup_time_lock);
	while (!timer_thread_cancel) {
		if (timer_cmp_time(wakeup_time, wakeup_time_max) >= 0) {
			pthread_cond_wait(&wakeup_time_cond, &wakeup_time_lock);
			continue;
		}

		const int wait_result = pthread_cond_timedwait(&wakeup_time_cond,
			&wakeup_time_lock, &wakeup_time);
		if (timer_thread_cancel)
			break;
		if (wait_result != ETIMEDOUT)
			continue;

		tm_time_t system_time;
		timer_current_time(system_time);
		if (timer_cmp_time(wakeup_time, system_time) <= 0) {
			wakeup_time = wakeup_time_max;
			pthread_mutex_unlock(&wakeup_time_lock);
			SetInterruptFlag(INTFLAG_TIMER);
			TriggerInterrupt();
			pthread_mutex_lock(&wakeup_time_lock);
		}
	}
	pthread_mutex_unlock(&wakeup_time_lock);
	return NULL;
}
#endif


/*
 *  Timer interrupt function (executed as part of 60Hz interrupt)
 */

void TimerInterrupt(void)
{
	// Look for active TMTasks that have expired
	tm_time_t now;
	timer_current_time(now);
	TMDesc *desc = tmDescList;
	while (desc) {
		TMDesc *next = desc->next;
		uint32 tm = desc->task;
		if ((ReadMacInt16(tm + qType) & 0x8000) && timer_cmp_time(desc->wakeup, now) <= 0) {

			// Found one, mark as inactive and remove it from the Time Manager queue
			WriteMacInt16(tm + qType, ReadMacInt16(tm + qType) & 0x7fff);
			dequeue_tm(tm);

			// Call timer function
			uint32 addr = ReadMacInt32(tm + tmAddr);
			if (addr) {
				D(bug("Calling TimeTask %08lx, addr %08lx\n", tm, addr));
				M68kRegisters r;
				r.a[0] = addr;
				r.a[1] = tm;
				Execute68k(r.a[0], &r);
				D(bug(" returned from TimeTask\n"));
			}
		}
		desc = next;
	}

#if PRECISE_TIMING
	// Look for next task to be called and set wakeup_time
#if PRECISE_TIMING_BEOS
	while (acquire_sem(wakeup_time_sem) == B_INTERRUPTED) ;
	suspend_thread(timer_thread);
#endif
#if PRECISE_TIMING_MACH
	semaphore_wait(wakeup_time_sem);
	thread_suspend(timer_thread);
#endif
#if PRECISE_TIMING_POSIX
	pthread_mutex_lock(&wakeup_time_lock);
#endif
	wakeup_time = wakeup_time_max;
	for (TMDesc *d = tmDescList; d; d = d->next)
		if ((ReadMacInt16(d->task + qType) & 0x8000))
			if (timer_cmp_time(d->wakeup, wakeup_time) < 0)
				wakeup_time = d->wakeup;
#if PRECISE_TIMING_BEOS
	release_sem(wakeup_time_sem);
	thread_info info;
	do {
		resume_thread(timer_thread);			// This will unblock the thread
		get_thread_info(timer_thread, &info);
	} while (info.state == B_THREAD_SUSPENDED);	// Sometimes, resume_thread() doesn't work (BeOS bug?)
#endif
#if PRECISE_TIMING_MACH
	semaphore_signal(wakeup_time_sem);
	thread_abort(timer_thread);
	thread_resume(timer_thread);
#endif
#if PRECISE_TIMING_POSIX
	pthread_mutex_unlock(&wakeup_time_lock);
	if (wakeup_time_cond_initialized)
		pthread_cond_signal(&wakeup_time_cond);
#endif
#endif
}
