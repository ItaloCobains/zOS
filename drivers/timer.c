/*
 * timer.c -- ARM Generic Timer driver.
 *
 * AArch64 has a built-in timer accessible via system registers.
 * We use the non-secure physical timer (EL1), which fires IRQ #30
 * through the GIC.
 *
 * The timer works by comparing a counter (CNTPCT_EL0, always incrementing)
 * against a compare value (CNTP_CVAL_EL0). When counter >= compare, the
 * interrupt fires. We set it to fire every TICK_INTERVAL ticks.
 *
 * Timer frequency on QEMU is typically 62.5 MHz (62,500,000 Hz).
 * We want ~10ms ticks, so interval = freq / 100.
 */

#include "timer.h"
#include "uart.h"

static uint64_t tick_interval;
static uint64_t tick_count;

/*
 * Read the timer frequency from CNTFRQ_EL0.
 */
static uint64_t read_timer_freq(void) {
  uint64_t freq;
  __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
  return freq;
}

/*
 * Read the current counter value.
 */
static uint64_t read_counter(void) {
  uint64_t cnt;
  __asm__ volatile("mrs %0, cntpct_el0" : "=r"(cnt));
  return cnt;
}

void timer_init(void) {
  uint64_t freq = read_timer_freq();
  tick_interval = freq / 100; /* ~10ms per tick */

  /* Set the compare value to now + interval */
  uint64_t next = read_counter() + tick_interval;
  __asm__ volatile("msr cntp_cval_el0, %0" : : "r"(next));

  /* Enable the timer: CNTP_CTL_EL0 bit 0 = enable, bit 1 = mask (0=unmasked) */
  __asm__ volatile("msr cntp_ctl_el0, %0" : : "r"((uint64_t)1));

  uart_puts("[timer] 10ms tick\n");
}

/*
 * Called from trap.c on timer interrupt.
 * Resets the timer for the next tick.
 */
void timer_handler(void) {
  tick_count++;
  /* Set next compare value */
  uint64_t next = read_counter() + tick_interval;
  __asm__ volatile("msr cntp_cval_el0, %0" : : "r"(next));
}

uint64_t timer_get_ticks(void) { return tick_count; }
