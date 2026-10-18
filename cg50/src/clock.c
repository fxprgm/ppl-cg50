#include "clock.h"

#include <stdbool.h>

#include <gint/clock.h>
#include <gint/mpu/tmu.h>
#include <gint/rtc.h>
#include <gint/timer.h>

volatile uint32_t clock_ms;

static int free_timer = -1, ms_timer = -1;
static uint32_t pphi;

static int keep_running(void)
{
	return TIMER_CONTINUE;
}

static int tick(void)
{
	clock_ms++;
	return TIMER_CONTINUE;
}

void clock_init(void)
{
	/* a TMU at Pphi/4 left running over its whole range (as addin/src/prof.c) */
	for (int i = 0; i < 3 && free_timer < 0; i++)
		free_timer = timer_configure(i | TIMER_Pphi_4, 0xffffffff, GINT_CALL(keep_running));
	if (free_timer >= 0)
		timer_start(free_timer);
	/* the casio-cg50 emulator doesn't model the clock registers gint reads (PLLCR): an
	   implausible Pphi means that, assume the stock 29.4912 MHz */
	pphi = clock_freq()->Pphi_f;
	if (pphi < 4000000 || pphi > 200000000)
		pphi = 29491200;
	/* only wakes the CPU from sleep(); time is measured with the TMU above (clock_real_ms).
	   gint picks an ETMU (32768 Hz, independent of the clock settings) */
	ms_timer = timer_configure(TIMER_ANY, 1000, GINT_CALL(tick));
	if (ms_timer >= 0)
		timer_start(ms_timer);
}

void clock_quit(void)
{
	if (ms_timer >= 0)
		timer_stop(ms_timer);
	if (free_timer >= 0)
		timer_stop(free_timer);
	ms_timer = free_timer = -1;
}

uint32_t clock_now(void)
{
	return free_timer >= 0 ? SH7305_TMU.TMU[free_timer].TCNT : 0;
}

uint32_t clock_real_ms(void)
{
	static uint32_t last;
	static uint64_t ticks;
	static bool started;
	uint32_t now = clock_now();
	if (started)
		ticks += (uint32_t)(last - now); /* counts down */
	started = true;
	last = now;
	return ticks * 4000 / pphi;
}

uint32_t clock_us(uint32_t start, uint32_t end)
{
	uint32_t ticks = start - end; /* counts down */
	return (uint64_t)ticks * 4000000 / pphi;
}

/* the CG50's clock in seconds since 2000-01-01 (0 if it isn't set to a plausible date) */
uint32_t clock_cg_seconds(void)
{
	rtc_time_t t;
	rtc_get_time(&t);
	if (t.year < 2000 || t.year > 2099 || t.month > 11 || t.month_day < 1)
		return 0;
	static const uint16_t before[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
	uint32_t y = t.year - 2000;
	uint32_t days = y * 365 + (y + 3) / 4 + before[t.month] + t.month_day - 1;
	if (t.month >= 2 && y % 4 == 0)
		days++;
	return ((days * 24 + t.hours) * 60 + t.minutes) * 60 + t.seconds;
}
