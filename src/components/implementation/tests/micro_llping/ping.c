#include <cos_component.h>
#include <cos_debug.h>

#include <pong.h>

#include <stdio.h>
#include <string.h>

int
prints(char *str)
{
	return 0;
}

int __attribute__((format(printf,1,2))) 
printc(char *fmt, ...)
{
	char s[128];
	va_list arg_ptr;
	int ret, len = 128;

	va_start(arg_ptr, fmt);
	ret = vsnprintf(s, len, fmt, arg_ptr);
	va_end(arg_ptr);
	ret = cos_print(s, ret);

	return ret;
}

#define ITER (10)//24*1024)//(100*1000)

unsigned long long tsc_start(void)
{
	unsigned long cycles_high, cycles_low; 
	asm volatile ("movl $0, %%eax\n\t"
		      "CPUID\n\t"
		      "RDTSC\n\t"
		      "movl %%edx, %0\n\t"
		      "movl %%eax, %1\n\t": "=r" (cycles_high), "=r" (cycles_low) :: 
		      "%eax", "%ebx", "%ecx", "%edx");

	return ((unsigned long long)cycles_high << 32) | cycles_low;
}

#include <ck_pr.h>
#define N_SYNC_CPU (NUM_CPU_COS)
int synced_nthd = 0;
void sync_all()
{
	int ret;

	ret = ck_pr_faa_int(&synced_nthd, 1);
	ret = (ret/N_SYNC_CPU + 1)*N_SYNC_CPU;
	while (ck_pr_load_int(&synced_nthd) < ret) ;
	
	return;
}

int all_exit = 0;
//#define MEAS_AVG
void pingpong(void)
{
	int i;
	long long diff;
	u64_t s, e, avg, avg2;
	u64_t sum = 0, max = 0, stddev_sum = 0;
	volatile u32_t last_tick, curr_tick;

//#ifdef MEAS_AVG
	printc("core %ld: doing pingpong IPC test\n", cos_cpuid());
	
	avg = 638; /* on the 40-core server.*/
	sum = 0;
	/////////////////
	last_tick = printc("FLUSH!!");
	/* printc("core %ld: doing pingpong w/ flush @tick %u!\n", cos_cpuid(), last_tick); */
	for (i = 0; i < ITER; i++) {
		s = tsc_start();
		call_cap(4, 0, 0, 0, 0);
		rdtscll(e);
//		if (e-s > 20000) printc("large: %llu @ %llu\n", e-s, e);
		curr_tick = printc("FLUSH!!");
		if (unlikely(curr_tick != last_tick)) {
//			printc("timer detected @ %llu, %d, %d, (cost %llu)\n", e, last_tick, curr_tick, e-s);
//			if (last_tick+1 != curr_tick) printc("tick diff > 1: %u, %u\n", last_tick,curr_tick);
			last_tick = curr_tick;
			i--;
			continue;
		}
		diff = e-s;
		sum += diff;

		diff = (diff-avg);
		stddev_sum += (diff*diff);

		if (max < e-s) max = e-s;
	}
	avg2 = sum /ITER;
	stddev_sum /= ITER;
	if (avg != avg2) printc(">>>>Warning: assumed average overhead not consistent with the measured number\n");
	printc("core %ld: @tick %u pingpong done, avg (%llu, %llu), max %llu, stddev^2 %llu\n", 
	       cos_cpuid(), curr_tick, avg, avg2, max, stddev_sum);

	////////////////////////////
	call_cap(4, 0, 0, 0, 0);
	rdtscll(s);
	for (i = 0; i < ITER; i++) {
		call_cap(4, 0, 0, 0, 0);
	}
	rdtscll(e);

	avg = (e-s)/ITER;
	printc("\n core %ld: %d IPCs done, avg cost %llu cycles (no interrupt filtering)\n\n", cos_cpuid(), ITER, avg);

	ck_pr_store_int(&all_exit, 1);

	return;
}

int arcv_ready[NUM_CPU];

#include <ck_pr.h>

struct record_per_core {
	int rcv;
	int snd_thd_created;
	char _pad[CACHE_LINE-sizeof(int)*2];
}CACHE_ALIGNED;

struct record_per_core received[NUM_CPU];

void rcv_thd(void)
{
	int ret;
	struct record_per_core *curr_rcv = &received[cos_cpuid()];
//	printc("core %ld: rcv thd %d ready in ping!\n", cos_cpuid(), cos_get_thd_id());

	while (1) {
		ret = call_cap(ACAP_BASE + captbl_idsize(CAP_ARCV)*cos_cpuid(),0,0,0,0);
		if (ret) {
			printc("ERROR: arcv ret %d", ret);
			printc("rcv thd %d switching back to alpha %ld!\n", 
			       cos_get_thd_id(), SCHED_CAPTBL_ALPHATHD_BASE + cos_cpuid()*captbl_idsize(CAP_THD));
			ret = cap_switch_thd(SCHED_CAPTBL_ALPHATHD_BASE + cos_cpuid()*captbl_idsize(CAP_THD));
		}
		ck_pr_store_int(&curr_rcv->rcv, curr_rcv->rcv + 1);
	}
}

char *shmem = (char *)(0x44c00000-PAGE_SIZE);

void cos_init(void)
{
	int i;
	int curr_tick, last_tick;
	u64_t s, e;

	if (received[cos_cpuid()].snd_thd_created) {
		rcv_thd();
		BUG();
		return;
	}
	received[cos_cpuid()].snd_thd_created = 1;

	cap_switch_thd(RCV_THD_CAP_BASE + captbl_idsize(CAP_THD)*cos_cpuid());

	//init rcv thd first.

	if (cos_cpuid() == 0) {
		pingpong();
		goto done;
	}

#if NUM_CPU > 2
//	else {	goto done; }
//	if (cos_cpuid() <= (NUM_CPU_COS-1 - SND_RCV_OFFSET)  && (cos_cpuid() % 4 == 0)) {
	if (0) {
//	if (cos_cpuid() == 0) {
//	if ((cos_cpuid() % 4 == 0)) {
		struct record_per_core *curr_rcv = &received[cos_cpuid()];
		int last = 0;
		int target = SND_RCV_OFFSET + cos_cpuid();
		u64_t s1, e1;
		volatile u64_t *pong_shmem = (u64_t *)&shmem[(target) * CACHE_LINE];
		u64_t sum = 0, sum2 = 0, max = 0, stddev = 0;
		u32_t avg;
		int diff;

		avg = 3050;
		while (ck_pr_load_int(&arcv_ready[target]) == 0) ;
//		printc("core %ld: start sending ipi\n", cos_cpuid());
		last_tick = printc("FLUSH!!");
		rdtscll(s);
		for (i = 0; i<ITER; i++) {
//			last = ck_pr_load_int(&curr_rcv->rcv);
			*pong_shmem = 0;
			rdtscll(s1);
			call_cap(ACAP_BASE + captbl_idsize(CAP_ASND)*target, 0, 0, 0, 0);
			/* rdtscll(e1); */
			/* sum2 += e1-s1; */

			while (*pong_shmem == 0) ;
//			while (ck_pr_load_int(&curr_rcv->rcv) == last) ;
//			rdtscll(e1);
//			sum2 += e1-s1;
			e1 = *pong_shmem;

			curr_tick = printc("FLUSH!!");
			if (unlikely(curr_tick != last_tick)) {
//			printc("timer detected @ %llu, %d, %d, (cost %llu)\n", e, last_tick, curr_tick, e-s);
//				if (last_tick+1 != curr_tick) printc("tick diff > 1: %u, %u\n", last_tick,curr_tick);
				last_tick = curr_tick;
				i--;
				continue;
			}

			if (unlikely(e1 < s1)) {
//				printc("e1 %llu < s1 %llu\n", e1, s1);
				i--;
				continue;
			} else {
				diff = e1-s1;

				if (diff > max) max = diff;

				sum += diff;
				diff -= avg;
				stddev += diff*diff;
			}
		}
		rdtscll(e);
//		printc("core %ld: ipi avg ( %llu, %llu ): %llu\n", cos_cpuid(), (e-s)/ITER, sum2/ITER, sum/ITER);
		printc("core %ld: ipi avg (sender %llu): %llu, max %llu, stddev^2 %llu\n", cos_cpuid(), sum2/ITER, sum/ITER, max, stddev / ITER);
		if (cos_cpuid() == 0) ck_pr_store_int(&all_exit, 1);
	} else { //if ((cos_cpuid() % 4 <= 1)
//		printc("core %ld: thd %d switching to pong thd\n", cos_cpuid(), cos_get_thd_id());
		arcv_ready[cos_cpuid()] = 1;
		//printc("core %ld: doing operations as interference\n", cos_cpuid());
		////////////////////////
		rdtscll(s);
		while (1) {
			//do op here to measure response time.
//			call_cap(4, 0, 0, 0, 0);
//			rdtscll(e);
//			if ((e-s)/(2000*1000*1000) > RUNTIME) break;
			if (ck_pr_load_int(&all_exit)) break;
		}
		printc("core %ld: exiting from ping\n", cos_cpuid());
	}
#endif
done:
	cap_switch_thd(SCHED_CAPTBL_ALPHATHD_BASE + cos_cpuid()*captbl_idsize(CAP_THD));

	call();

	return;
	/* u64_t start, end, avg, tot = 0, dev = 0; */
	/* int i, j; */


/* 	printc("cpu %ld, thd %d from ping\n",cos_cpuid(), cos_get_thd_id()); */
/* //	call(111,222,333,444);			/\* get stack *\/ */

/* 	printc("core %ld: spinning....\n", cos_cpuid()); */
/* 	delay(); */
/* 	printc("core %ld: after spin!\n", cos_cpuid()); */
	
/* //	call(1111,2222,3333,4444);			/\* get stack *\/ */
/* 	printc("Starting %d Invocations.\n", ITER); */
	
/* 	int params[8] = {1,2,3,4, 11, 22, 33, 44}; */
/* 	par_inv(test_fn, (void *)params, 2); */

/* 	return; */

/* 	for (i = 0 ; i < ITER ; i++) { */
/* 		rdtscll(start); */
/* //		cos_send_ipi(i, 0, 0, 0); */
/* 		call(1,2,3,4); */
/* 		rdtscll(end); */
/* 		meas[i] = end-start; */
/* 	} */

/* 	for (i = 0 ; i < ITER ; i++) tot += meas[i]; */
/* 	avg = tot/ITER; */
/* 	printc("avg %lld\n", avg); */
/* 	for (tot = 0, i = 0, j = 0 ; i < ITER ; i++) { */
/* 		if (meas[i] < avg*2) { */
/* 			tot += meas[i]; */
/* 			j++; */
/* 		} */
/* 	} */
/* 	printc("avg w/o %d outliers %lld\n", ITER-j, tot/j); */

/* 	for (i = 0 ; i < ITER ; i++) { */
/* 		u64_t diff = (meas[i] > avg) ?  */
/* 			meas[i] - avg :  */
/* 			avg - meas[i]; */
/* 		dev += (diff*diff); */
/* 	} */
/* 	dev /= ITER; */
/* 	printc("deviation^2 = %lld\n", dev); */

/* 	printc("last invocation...\n"); */
/* 	rdtscll(start); */
/* 	int rrr = call(11,22,33,44); */
/* 	rdtscll(end); */
/* 	printc("done ret %d. cost %llu \n", rrr, end-start); */

/* 	rdtscll(start); */
/* 	rrr = call(11,22,33,44); */
/* 	rdtscll(end); */
/* 	printc("done ret %d. cost %llu \n", rrr, end-start); */

/* //	printc("%d invocations took %lld\n", ITER, end-start); */
/* 	return; */
}
