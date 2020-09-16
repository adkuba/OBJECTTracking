/***************************************************************************//**
 * @file
 * @brief SLEEPTIMER API implementation.
 *******************************************************************************
 * # License
 * <b>Copyright 2018 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/

#include <time.h>
#include <stdlib.h>

#include "em_device.h"
#include "em_common.h"
#include "em_core.h"
#include "sl_sleeptimer.h"
#include "sl_sleeptimer_hal.h"

#define TIME_UNIX_EPOCH                         (1970u)
#define TIME_NTP_EPOCH                          (1900u)
#define TIME_ZIGBEE_EPOCH                       (2000u)
#define TIME_NTP_UNIX_EPOCH_DIFF                (TIME_UNIX_EPOCH - TIME_NTP_EPOCH)
#define TIME_ZIGBEE_UNIX_EPOCH_DIFF             (TIME_ZIGBEE_EPOCH - TIME_UNIX_EPOCH)
#define TIME_DAY_COUNT_NTP_TO_UNIX_EPOCH        (TIME_NTP_UNIX_EPOCH_DIFF * 365u + 17u)                  ///< 70 years and 17 leap days
#define TIME_DAY_COUNT_ZIGBEE_TO_UNIX_EPOCH     (TIME_ZIGBEE_UNIX_EPOCH_DIFF * 365u + 7u)                ///< 30 years and 7 leap days
#define TIME_SEC_PER_DAY                        (60u * 60u * 24u)
#define TIME_NTP_EPOCH_OFFSET_SEC               (TIME_DAY_COUNT_NTP_TO_UNIX_EPOCH * TIME_SEC_PER_DAY)
#define TIME_ZIGBEE_EPOCH_OFFSET_SEC            (TIME_DAY_COUNT_ZIGBEE_TO_UNIX_EPOCH * TIME_SEC_PER_DAY)
#define TIME_DAY_PER_YEAR                       (365u)
#define TIME_SEC_PER_YEAR                       (TIME_SEC_PER_DAY * TIME_DAY_PER_YEAR)
#define TIME_UNIX_TIMESTAMP_MAX                 (0x7FFFFFFF)
#define TIME_UNIX_YEAR_MAX                      (2038u - TIME_NTP_EPOCH)                                 ///< Max UNIX year based from a 1900 epoch

#define TIME_LEAP_DAYS_UP_TO_YEAR(year)         (((year - 3) / 4) + 1)

/// @brief Time Format.
SLEEPTIMER_ENUM(sl_sleeptimer_time_format_t) {
  TIME_FORMAT_UNIX = 0,           ///< Number of seconds since January 1, 1970, 00:00. Type is signed, so represented on 31 bit.
  TIME_FORMAT_NTP = 1,            ///< Number of seconds since January 1, 1900, 00:00. Type is unsigned, so represented on 32 bit.
  TIME_FORMAT_ZIGBEE_CLUSTER = 2, ///< Number of seconds since January 1, 2000, 00:00. Type is unsigned, so represented on 32 bit.
};

/// tick_count, it can wrap around.
typedef uint32_t sl_sleeptimer_tick_count_t;

// Overflow counter used to provide 64-bits tick count.
static volatile uint8_t overflow_counter;

#if SL_SLEEPTIMER_WALLCLOCK_CONFIG
// Current time count.
static sl_sleeptimer_timestamp_t second_count;
// Tick rest when the frequency is not a divider of the timer width.
static uint32_t overflow_tick_rest = 0;
// Current time zone offset.
static sl_sleeptimer_time_zone_offset_t tz_offset = 0;
// Precalculated tick rest in case of overflow.
static uint32_t calculated_tick_rest = 0;
// Precalculated timer overflow duration in seconds.
static uint32_t calculated_sec_count = 0;
#endif

// Head of timer list.
static sl_sleeptimer_timer_handle_t *timer_head;

// Count at last update of delta of first timer.
static sl_sleeptimer_tick_count_t last_delta_update_count;

// Initialization flag.
static bool is_sleeptimer_initialized = false;

// Precalculated value to avoid millisecond to tick conversion overflow.
static uint32_t max_millisecond_conversion;

static void delta_list_insert_timer(sl_sleeptimer_timer_handle_t *handle,
                                    sl_sleeptimer_tick_count_t timeout);

static sl_status_t delta_list_remove_timer(sl_sleeptimer_timer_handle_t *handle);

static void set_comparator_for_next_timer(void);

static void update_first_timer_delta(void);

__STATIC_INLINE uint32_t div_to_log2(uint32_t div);

__STATIC_INLINE bool is_power_of_2(uint32_t nbr);

static sl_status_t create_timer(sl_sleeptimer_timer_handle_t *handle,
                                sl_sleeptimer_tick_count_t timeout_initial,
                                sl_sleeptimer_tick_count_t timeout_periodic,
                                sl_sleeptimer_timer_callback_t callback,
                                void *callback_data,
                                uint8_t priority,
                                uint16_t option_flags);

static void delay_callback(sl_sleeptimer_timer_handle_t *handle,
                           void *data);

#if SL_SLEEPTIMER_WALLCLOCK_CONFIG
static bool is_leap_year(uint16_t year);

static sl_sleeptimer_weekDay_t compute_day_of_week(uint32_t day);
static uint16_t compute_day_of_year(sl_sleeptimer_month_t month, uint8_t day, bool isLeapYear);

static bool is_valid_time(sl_sleeptimer_timestamp_t time,
                          sl_sleeptimer_time_format_t format,
                          sl_sleeptimer_time_zone_offset_t time_zone);

static bool is_valid_date(sl_sleeptimer_date_t *date);

static const uint8_t days_in_month[2u][12] = {
  /* Jan  Feb  Mar  Apr  May  Jun  Jul  Aug  Sep  Oct  Nov  Dec */
  { 31u, 28u, 31u, 30u, 31u, 30u, 31u, 31u, 30u, 31u, 30u, 31u },
  { 31u, 29u, 31u, 30u, 31u, 30u, 31u, 31u, 30u, 31u, 30u, 31u }
};
#endif

/**************************************************************************//**
 * Initializes sleep timer.
 *****************************************************************************/
sl_status_t sl_sleeptimer_init(void)
{
  CORE_DECLARE_IRQ_STATE;

  CORE_ENTER_ATOMIC();
  if (!is_sleeptimer_initialized) {
    timer_head  = NULL;
    last_delta_update_count = 0u;
    overflow_counter = 0u;
    sleeptimer_hal_init_timer();
    sleeptimer_hal_enable_int(SLEEPTIMER_EVENT_OF);

#if SL_SLEEPTIMER_WALLCLOCK_CONFIG
    second_count = 0;
    calculated_tick_rest = ((uint64_t)UINT32_MAX + 1) % (uint64_t)sleeptimer_hal_get_timer_frequency();
    calculated_sec_count = (((uint64_t)UINT32_MAX + 1) / (uint64_t)sleeptimer_hal_get_timer_frequency());
#endif
    max_millisecond_conversion = ((uint64_t)UINT32_MAX * (uint64_t)1000u) / sleeptimer_hal_get_timer_frequency();
    is_sleeptimer_initialized = true;
  }
  CORE_EXIT_ATOMIC();

  return SL_STATUS_OK;
}

/**************************************************************************//**
 * Starts a 32 bits timer.
 *****************************************************************************/
sl_status_t sl_sleeptimer_start_timer(sl_sleeptimer_timer_handle_t *handle,
                                      uint32_t timeout,
                                      sl_sleeptimer_timer_callback_t callback,
                                      void *callback_data,
                                      uint8_t priority,
                                      uint16_t option_flags)
{
  bool is_running = false;

  if (handle == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  sl_sleeptimer_is_timer_running(handle, &is_running);
  if (is_running == true) {
    return SL_STATUS_NOT_READY;
  }

  return create_timer(handle,
                      timeout,
                      0,
                      callback,
                      callback_data,
                      priority,
                      option_flags);
}

/**************************************************************************//**
 * Restarts a 32 bits timer.
 *****************************************************************************/
sl_status_t sl_sleeptimer_restart_timer(sl_sleeptimer_timer_handle_t *handle,
                                        uint32_t timeout,
                                        sl_sleeptimer_timer_callback_t callback,
                                        void *callback_data,
                                        uint8_t priority,
                                        uint16_t option_flags)
{
  if (handle == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  //Trying to stop the Timer. Failing to do so implies the timer is not running.
  sl_sleeptimer_stop_timer(handle);

  //Creates the timer in any case.
  return create_timer(handle,
                      timeout,
                      0,
                      callback,
                      callback_data,
                      priority,
                      option_flags);
}

/**************************************************************************//**
 * Starts a 32 bits periodic timer.
 *****************************************************************************/
sl_status_t sl_sleeptimer_start_periodic_timer(sl_sleeptimer_timer_handle_t *handle,
                                               uint32_t timeout,
                                               sl_sleeptimer_timer_callback_t callback,
                                               void *callback_data,
                                               uint8_t priority,
                                               uint16_t option_flags)
{
  bool is_running = false;

  if (handle == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  sl_sleeptimer_is_timer_running(handle, &is_running);
  if (is_running == true) {
    return SL_STATUS_INVALID_STATE;
  }

  return create_timer(handle,
                      timeout,
                      timeout,
                      callback,
                      callback_data,
                      priority,
                      option_flags);
}

/**************************************************************************//**
 * Restarts a 32 bits periodic timer.
 *****************************************************************************/
sl_status_t sl_sleeptimer_restart_periodic_timer(sl_sleeptimer_timer_handle_t *handle,
                                                 uint32_t timeout,
                                                 sl_sleeptimer_timer_callback_t callback,
                                                 void *callback_data,
                                                 uint8_t priority,
                                                 uint16_t option_flags)
{
  if (handle == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  //Trying to stop the Timer. Failing to do so implies the timer has already been stopped.
  sl_sleeptimer_stop_timer(handle);

  //Creates the timer in any case.
  return create_timer(handle,
                      timeout,
                      timeout,
                      callback,
                      callback_data,
                      priority,
                      option_flags);
}

/**************************************************************************//**
 * Stops a 32 bits timer.
 *****************************************************************************/
sl_status_t sl_sleeptimer_stop_timer(sl_sleeptimer_timer_handle_t *handle)
{
  CORE_DECLARE_IRQ_STATE;
  sl_status_t error;
  bool set_comparator = false;

  if (handle == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  CORE_ENTER_ATOMIC();
  update_first_timer_delta();

  // If first timer in list, update timer comparator.
  if (timer_head == handle) {
    sleeptimer_hal_disable_int(SLEEPTIMER_EVENT_COMP);
    set_comparator = true;
  }

  error = delta_list_remove_timer(handle);
  if (error != SL_STATUS_OK) {
    CORE_EXIT_ATOMIC();
    return error;
  }

  if (set_comparator && timer_head) {
    set_comparator_for_next_timer();
  }

  CORE_EXIT_ATOMIC();
  return SL_STATUS_OK;
}

/**************************************************************************//**
 * Gets the status of a timer.
 *****************************************************************************/
sl_status_t sl_sleeptimer_is_timer_running(sl_sleeptimer_timer_handle_t *handle,
                                           bool *running)
{
  CORE_DECLARE_IRQ_STATE;
  sl_sleeptimer_timer_handle_t *current;

  if (handle == NULL || running == NULL) {
    return SL_STATUS_NULL_POINTER;
  } else {
    *running = false;
    CORE_ENTER_ATOMIC();
    current = timer_head;
    while (current != NULL && !*running) {
      if (current == handle) {
        *running = true;
      } else {
        current = current->next;
      }
    }
    CORE_EXIT_ATOMIC();
  }
  return SL_STATUS_OK;
}

/**************************************************************************//**
 * Gets a 32 bits timer's time remaining.
 *****************************************************************************/
sl_status_t sl_sleeptimer_get_timer_time_remaining(sl_sleeptimer_timer_handle_t *handle,
                                                   uint32_t *time)
{
  CORE_DECLARE_IRQ_STATE;
  sl_sleeptimer_timer_handle_t *current;

  if (handle == NULL || time == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  CORE_ENTER_ATOMIC();

  update_first_timer_delta();
  *time  = handle->delta;

  // Retrieve timer in list and add the deltas.
  current = timer_head;
  while (current != handle && current != NULL) {
    *time += current->delta;
    current = current->next;
  }

  if (current != handle) {
    CORE_EXIT_ATOMIC();
    return SL_STATUS_NOT_READY;
  }

  // Substract time since last compare match.
  if (*time > sleeptimer_hal_get_counter() - last_delta_update_count) {
    *time -= sleeptimer_hal_get_counter() - last_delta_update_count;
  } else {
    *time = 0;
  }

  CORE_EXIT_ATOMIC();

  return SL_STATUS_OK;
}

/**************************************************************************//**
 * Gets the time remaining until the first timer with the matching set of flags
 * expires.
 *****************************************************************************/
sl_status_t sl_sleeptimer_get_remaining_time_of_first_timer(uint16_t option_flags,
                                                            uint32_t *time_remaining)
{
  CORE_DECLARE_IRQ_STATE;
  sl_sleeptimer_timer_handle_t *current;
  uint32_t time = 0;

  CORE_ENTER_ATOMIC();
  // parse list and retrieve first timer with HF requirement.
  current = timer_head;
  while (current != NULL) {
    // save time remaining for timer.
    time += current->delta;
    // Check if the current timer has the flags requested
    if (current->option_flags == option_flags) {
      *time_remaining = time;
      CORE_EXIT_ATOMIC();
      return SL_STATUS_OK;
    }
    current = current->next;
  }
  CORE_EXIT_ATOMIC();

  return SL_STATUS_EMPTY;
}

/***************************************************************************//**
* Gets current 32 bits tick count.
*******************************************************************************/
uint32_t sl_sleeptimer_get_tick_count(void)
{
  return sleeptimer_hal_get_counter();
}

/***************************************************************************//**
* Gets current 64 bits tick count.
*******************************************************************************/
uint64_t sl_sleeptimer_get_tick_count64(void)
{
  CORE_DECLARE_IRQ_STATE;

  uint64_t tc = sleeptimer_hal_get_counter();
  CORE_ENTER_ATOMIC();
  tc |= ((uint64_t)overflow_counter << 32);
  CORE_EXIT_ATOMIC();

  return tc;
}

/***************************************************************************//**
 * Get timer frequency.
 ******************************************************************************/
uint32_t sl_sleeptimer_get_timer_frequency(void)
{
  return sleeptimer_hal_get_timer_frequency();
}

#if SL_SLEEPTIMER_WALLCLOCK_CONFIG
/***************************************************************************//**
 * Retrieves current time.
 ******************************************************************************/
sl_sleeptimer_timestamp_t sl_sleeptimer_get_time(void)
{
  uint32_t cnt = 0u;
  uint32_t freq = 0u;
  sl_sleeptimer_timestamp_t time;
  CORE_DECLARE_IRQ_STATE;

  cnt = sleeptimer_hal_get_counter();
  freq = sl_sleeptimer_get_timer_frequency();

  CORE_ENTER_ATOMIC();
  time = second_count + cnt / freq;
  if (cnt % freq + overflow_tick_rest >= freq) {
    time++;
  }
  CORE_EXIT_ATOMIC();

  return time;
}

/***************************************************************************//**
 * Sets current time.
 ******************************************************************************/
sl_status_t sl_sleeptimer_set_time(sl_sleeptimer_timestamp_t time)
{
  uint32_t freq = 0u;
  uint32_t counter_sec = 0u;
  uint32_t cnt = 0;
  CORE_DECLARE_IRQ_STATE;

  if (!is_valid_time(time, TIME_FORMAT_UNIX, 0u)) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  freq = sl_sleeptimer_get_timer_frequency();
  cnt = sleeptimer_hal_get_counter();

  CORE_ENTER_ATOMIC();
  second_count = time;
  overflow_tick_rest = 0;
  counter_sec = cnt / freq;

  if (second_count >= counter_sec) {
    second_count -= counter_sec;
  } else {
    CORE_EXIT_ATOMIC();
    return SL_STATUS_INVALID_PARAMETER;
  }

  CORE_EXIT_ATOMIC();

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Gets current date.
 ******************************************************************************/
sl_status_t sl_sleeptimer_get_datetime(sl_sleeptimer_date_t *date)
{
  sl_sleeptimer_timestamp_t time = 0u;
  sl_sleeptimer_time_zone_offset_t tz;
  sl_status_t err_code = SL_STATUS_OK;

  time = sl_sleeptimer_get_time();
  tz = sl_sleeptimer_get_tz();
  err_code = sl_sleeptimer_convert_time_to_date(time, tz, date);

  return err_code;
}

/***************************************************************************//**
 * Sets current time, in date format.
 ******************************************************************************/
sl_status_t sl_sleeptimer_set_datetime(sl_sleeptimer_date_t *date)
{
  sl_sleeptimer_timestamp_t time = 0u;
  sl_status_t err_code = SL_STATUS_OK;

  if (!is_valid_date(date)) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  err_code = sl_sleeptimer_convert_date_to_time(date, &time);

  if (err_code != SL_STATUS_OK) {
    return err_code;
  }

  err_code = sl_sleeptimer_set_time(time);
  return err_code;
}

/***************************************************************************//**
 * Builds a date time structure based on the provided parameters.
 ******************************************************************************/
sl_status_t sl_sleeptimer_build_datetime(sl_sleeptimer_date_t *date,
                                         uint16_t year,
                                         sl_sleeptimer_month_t month,
                                         uint8_t month_day,
                                         uint8_t hour,
                                         uint8_t min,
                                         uint8_t sec,
                                         sl_sleeptimer_time_zone_offset_t tz_offset)
{
  if (date == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  // If year is smaller than 1900, assume NTP Epoch is used.
  date->year = ((year < TIME_NTP_EPOCH) ? year : (year - TIME_NTP_EPOCH));
  date->month = month;
  date->month_day = month_day;
  date->hour = hour;
  date->min = min;
  date->sec = sec;
  date->time_zone = tz_offset;

  // Validate that input parameters are correct before filing the missing fields
  if (!is_valid_date(date)) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  date->day_of_year = compute_day_of_year(date->month, date->month_day, is_leap_year(date->year));
  date->day_of_week = compute_day_of_week(((date->year - TIME_NTP_UNIX_EPOCH_DIFF)  * TIME_DAY_PER_YEAR)
                                          + TIME_LEAP_DAYS_UP_TO_YEAR(date->year - TIME_NTP_UNIX_EPOCH_DIFF)
                                          + date->day_of_year - 1);

  return SL_STATUS_OK;
}

/*******************************************************************************
 * Convert a time stamp into a date structure.
 ******************************************************************************/
sl_status_t sl_sleeptimer_convert_time_to_date(sl_sleeptimer_timestamp_t time,
                                               sl_sleeptimer_time_zone_offset_t time_zone,
                                               sl_sleeptimer_date_t *date)
{
  uint8_t full_year = 0;
  uint8_t leap_day = 0;
  uint8_t leap_year_flag = 0;
  uint8_t current_month = 0;

  if (!is_valid_time(time, TIME_FORMAT_UNIX, time_zone)) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  date->sec = time % 60;
  time /= 60;
  date->min = time % 60;
  time /= 60;
  date->hour = time % 24;
  time /= 24; // time is now the number of days since 1970.

  date->day_of_week = (sl_sleeptimer_weekDay_t)compute_day_of_week(time);

  full_year = time / (TIME_DAY_PER_YEAR); // Approximates the number of full years.
  if (full_year > 2) {
    leap_day = TIME_LEAP_DAYS_UP_TO_YEAR(full_year);  // Approximates the number of leap days.
    full_year = (time - leap_day) / (TIME_DAY_PER_YEAR); // Computes the number of year integrating the leap days.
    leap_day = TIME_LEAP_DAYS_UP_TO_YEAR(full_year);  // Computes the actual number of leap days of the previous years.
  }
  date->year = TIME_NTP_UNIX_EPOCH_DIFF + full_year; // Year in date struct must be based on a 1900 epoch.
  if (is_leap_year(date->year)) {
    leap_year_flag = 1;
  }

  time = (time - leap_day) - (TIME_DAY_PER_YEAR * full_year);  // Subtracts days of previous year.
  date->day_of_year = time + 1;

  while (time >= days_in_month[leap_year_flag][current_month]) {
    time -= days_in_month[leap_year_flag][current_month]; // Subtracts the number of days of the passed month.
    current_month++;
  }
  date->month = (sl_sleeptimer_month_t) current_month;
  date->month_day = time + 1;
  date->time_zone = time_zone;

  return SL_STATUS_OK;
}

/*******************************************************************************
 * Convert a date structure into a time stamp.
 ******************************************************************************/
sl_status_t sl_sleeptimer_convert_date_to_time(sl_sleeptimer_date_t *date,
                                               sl_sleeptimer_timestamp_t *time)
{
  uint16_t month_days = 0;
  uint8_t  month;
  uint8_t  full_year = 0;
  uint8_t  leap_year_flag = 0;
  uint8_t  leap_days = 0;
  if (!is_valid_date(date)) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  full_year = (date->year - TIME_NTP_UNIX_EPOCH_DIFF);       // Timestamp returned must follow the UNIX epoch.
  month = (uint8_t)date->month;                              // offset to get months value from 1 to 12.

  *time = full_year * TIME_SEC_PER_YEAR;

  if (full_year > 2) {
    leap_days = TIME_LEAP_DAYS_UP_TO_YEAR(full_year);
    month_days = leap_days;
  }

  if (is_leap_year(date->year)) {
    leap_year_flag = 1;
  }

  for (int i = 0; i < month; i++) {
    month_days += days_in_month[leap_year_flag][i];         // Add the number of days of the month of the year.
  }

  month_days += (date->month_day - 1);                       // Add full days of the current month.
  *time += month_days * TIME_SEC_PER_DAY;
  *time += (3600 * date->hour) + (60 * date->min) + date->sec;
  *time += date->time_zone;

  return SL_STATUS_OK;
}

/*******************************************************************************
 * Convert a date structure to string.
 ******************************************************************************/
uint32_t sl_sleeptimer_convert_date_to_str(char *str,
                                           size_t size,
                                           const uint8_t *format,
                                           sl_sleeptimer_date_t *date)
{
  uint32_t  return_size = 0u;
  if (is_valid_date(date)) {
    struct tm date_struct;

    date_struct.tm_hour = date->hour;
    date_struct.tm_mday = date->month_day;
    date_struct.tm_min = date->min;
    date_struct.tm_mon = date->month;
    date_struct.tm_sec = date->sec;
    date_struct.tm_wday = date->day_of_week;
    date_struct.tm_yday = date->day_of_year;
    date_struct.tm_year = date->year;

    return_size = strftime(str,
                           size,
                           (const char *)format,
                           &date_struct);
  }

  return (return_size);
}

/***************************************************************************//**
 * Sets time zone offset.
 *
 * @param  offset  Time zone offset, in seconds.
 ******************************************************************************/
void sl_sleeptimer_set_tz(sl_sleeptimer_time_zone_offset_t offset)
{
  CORE_DECLARE_IRQ_STATE;

  CORE_ENTER_ATOMIC();
  tz_offset = offset;
  CORE_EXIT_ATOMIC();
}

/***************************************************************************//**
 * Gets time zone offset.
 *
 * @return Time zone offset, in seconds.
 ******************************************************************************/
sl_sleeptimer_time_zone_offset_t sl_sleeptimer_get_tz(void)
{
  sl_sleeptimer_time_zone_offset_t offset;
  CORE_DECLARE_IRQ_STATE;

  CORE_ENTER_ATOMIC();
  offset = tz_offset;
  CORE_EXIT_ATOMIC();

  return offset;
}

/***************************************************************************//**
 * Converts Unix timestamp into NTP timestamp.
 ******************************************************************************/
sl_status_t sl_sleeptimer_convert_unix_time_to_ntp(sl_sleeptimer_timestamp_t time,
                                                   uint32_t *ntp_time)
{
  uint32_t temp_ntp_time;
  temp_ntp_time = time + TIME_NTP_EPOCH_OFFSET_SEC;
  if (!is_valid_time(temp_ntp_time, TIME_FORMAT_NTP, 0u)) {
    return SL_STATUS_INVALID_PARAMETER;
  } else {
    *ntp_time = temp_ntp_time;
    return SL_STATUS_OK;
  }
}

/***************************************************************************//**
 * Converts NTP timestamp into Unix timestamp.
 ******************************************************************************/
sl_status_t sl_sleeptimer_convert_ntp_time_to_unix(uint32_t ntp_time,
                                                   sl_sleeptimer_timestamp_t *time)
{
  uint32_t temp_time;
  temp_time = ntp_time - TIME_NTP_EPOCH_OFFSET_SEC;
  if (!is_valid_time(temp_time, TIME_FORMAT_UNIX, 0u)) {
    return SL_STATUS_INVALID_PARAMETER;
  } else {
    *time = temp_time;
    return SL_STATUS_OK;
  }
}

/***************************************************************************//**
 * Converts Unix timestamp into Zigbee timestamp.
 ******************************************************************************/
sl_status_t sl_sleeptimer_convert_unix_time_to_zigbee(sl_sleeptimer_timestamp_t time,
                                                      uint32_t *zigbee_time)
{
  uint32_t temp_zigbee_time;
  temp_zigbee_time = time - TIME_ZIGBEE_EPOCH_OFFSET_SEC;
  if (!is_valid_time(temp_zigbee_time, TIME_FORMAT_ZIGBEE_CLUSTER, 0u)) {
    return SL_STATUS_INVALID_PARAMETER;
  } else {
    *zigbee_time = temp_zigbee_time;
    return SL_STATUS_OK;
  }
}

/***************************************************************************//**
 * Converts Zigbee timestamp into Unix timestamp.
 ******************************************************************************/
sl_status_t sl_sleeptimer_convert_zigbee_time_to_unix(uint32_t zigbee_time,
                                                      sl_sleeptimer_timestamp_t *time)
{
  uint32_t temp_time;
  temp_time = zigbee_time + TIME_ZIGBEE_EPOCH_OFFSET_SEC;
  if (!is_valid_time(temp_time, TIME_FORMAT_UNIX, 0u)) {
    return SL_STATUS_INVALID_PARAMETER;
  } else {
    *time = temp_time;
    return SL_STATUS_OK;
  }
}

#endif // SL_SLEEPTIMER_WALLCLOCK_CONFIG

/*******************************************************************************
 * Active delay of 'time_ms' milliseconds.
 ******************************************************************************/
void sl_sleeptimer_delay_millisecond(uint16_t time_ms)
{
  volatile bool wait = true;
  sl_status_t error_code;
  sl_sleeptimer_timer_handle_t delay_timer;
  uint32_t delay = sl_sleeptimer_ms_to_tick(time_ms);

  error_code = sl_sleeptimer_start_timer(&delay_timer,
                                         delay,
                                         delay_callback,
                                         (void *)&wait,
                                         0,
                                         0);
  if (error_code == SL_STATUS_OK) {
    while (wait) { // Active delay loop.
    }
  }
}

/*******************************************************************************
 * Converts milliseconds in ticks.
 ******************************************************************************/
uint32_t sl_sleeptimer_ms_to_tick(uint16_t time_ms)
{
  return (uint32_t)((((uint32_t)time_ms * sleeptimer_hal_get_timer_frequency()) / 1000) + 1);
}

/*******************************************************************************
 * Converts 32-bits milliseconds in ticks.
 ******************************************************************************/
sl_status_t sl_sleeptimer_ms32_to_tick(uint32_t time_ms,
                                       uint32_t *tick)
{
  if (time_ms <= max_millisecond_conversion) {
    *tick = (uint32_t)((((uint64_t)time_ms * sleeptimer_hal_get_timer_frequency()) / 1000u) + 1);
    return SL_STATUS_OK;
  } else {
    return SL_STATUS_INVALID_PARAMETER;
  }
}

/*******************************************************************************
 * Converts ticks in milliseconds.
 ******************************************************************************/
uint32_t sl_sleeptimer_tick_to_ms(uint32_t tick)
{
  if (is_power_of_2(sleeptimer_hal_get_timer_frequency())) {
    return (uint32_t)(((uint64_t)tick * (uint64_t)1000u) >> div_to_log2(sleeptimer_hal_get_timer_frequency()));
  } else {
    return (uint32_t)(((uint64_t)tick * (uint64_t)1000u) / sleeptimer_hal_get_timer_frequency());
  }
}

/*******************************************************************************
 * Converts 64-bits ticks in milliseconds.
 ******************************************************************************/
sl_status_t sl_sleeptimer_tick64_to_ms(uint64_t tick,
                                       uint64_t *ms)
{
  if (tick <= UINT64_MAX / 1000) {
    if (is_power_of_2(sleeptimer_hal_get_timer_frequency())) {
      *ms =  (uint64_t)(((uint64_t)tick * (uint64_t)1000u) >> div_to_log2(sleeptimer_hal_get_timer_frequency()));
      return SL_STATUS_OK;
    } else {
      *ms = (uint64_t)(((uint64_t)tick * (uint64_t)1000u) / sleeptimer_hal_get_timer_frequency());
      return SL_STATUS_OK;
    }
  } else {
    return SL_STATUS_INVALID_PARAMETER;
  }
}
/*******************************************************************************
 * Process timer interrupt.
 *
 * @param local_flag Flag indicating the type of timer interrupt.
 ******************************************************************************/
void process_timer_irq(uint8_t local_flag)
{
  CORE_DECLARE_IRQ_STATE;
  if (local_flag & SLEEPTIMER_EVENT_OF) {
#if SL_SLEEPTIMER_WALLCLOCK_CONFIG
    uint32_t timer_freq = sl_sleeptimer_get_timer_frequency();

    overflow_tick_rest += calculated_tick_rest;
    if (overflow_tick_rest >= timer_freq) {
      second_count++;
      overflow_tick_rest -= timer_freq;
    }
    second_count = second_count + calculated_sec_count;
#endif
    overflow_counter++;

    update_first_timer_delta();
    set_comparator_for_next_timer();
  }

  if (local_flag & SLEEPTIMER_EVENT_COMP) {
    sl_sleeptimer_tick_count_t delta_tot = 0u;
    sl_sleeptimer_tick_count_t current_cnt = sleeptimer_hal_get_counter();

    delta_tot = current_cnt - last_delta_update_count;

    CORE_ENTER_ATOMIC();
    // Process all timers that have expired.
    while ((timer_head) && (delta_tot >= timer_head->delta)) {
      sl_sleeptimer_tick_count_t new_cnt;
      sl_sleeptimer_tick_count_t delta_tot_temp = delta_tot;
      sl_sleeptimer_timer_handle_t *current = timer_head;
      sl_sleeptimer_timer_handle_t *temp = timer_head;

      last_delta_update_count = current_cnt;

      while ((temp != NULL) && (delta_tot_temp >= temp->delta)) {
        if (current->priority > temp->priority) {
          current = temp;
        }

        delta_tot_temp -= temp->delta;
        temp = temp->next;
      }
      CORE_EXIT_ATOMIC();
      delta_tot -= current->delta;
      current->delta = 0;

      CORE_ENTER_ATOMIC();
      delta_list_remove_timer(current);
      CORE_EXIT_ATOMIC();

      if (current->timeout_periodic != 0u) {
        CORE_ENTER_ATOMIC();
        delta_list_insert_timer(current, current->timeout_periodic);
        CORE_EXIT_ATOMIC();
      }

      if (current->callback != NULL) {
        current->callback(current, current->callback_data);
      }

      new_cnt = sleeptimer_hal_get_counter();
      delta_tot += new_cnt - current_cnt;
      current_cnt = new_cnt;
      CORE_ENTER_ATOMIC();
    }

    if (timer_head) {
      timer_head->delta -= delta_tot;
      last_delta_update_count = current_cnt;

      set_comparator_for_next_timer();
    } else {
      sleeptimer_hal_disable_int(SLEEPTIMER_EVENT_COMP);
    }
    CORE_EXIT_ATOMIC();
  }
}

/*******************************************************************************
 * Timer expiration callback for the delay function.
 *
 * @param handle Pointer to handle to timer.
 * @param data Pointer to delay flag.
 ******************************************************************************/
static void delay_callback(sl_sleeptimer_timer_handle_t *handle,
                           void *data)
{
  volatile bool *wait_flag = (bool *)data;

  (void)handle;  // Unused parameter.

  *wait_flag = false;
}

/*******************************************************************************
 * Inserts a timer in the delta list.
 *
 * @param handle Pointer to handle to timer.
 * @param timeout Timer timeout, in ticks.
 ******************************************************************************/
static void delta_list_insert_timer(sl_sleeptimer_timer_handle_t *handle,
                                    sl_sleeptimer_tick_count_t timeout)
{
  sl_sleeptimer_tick_count_t local_handle_delta = timeout;

  handle->delta = local_handle_delta;

  if (timer_head != NULL) {
    sl_sleeptimer_timer_handle_t *prev = NULL;
    sl_sleeptimer_timer_handle_t *current = timer_head;
    // Find timer position taking into accounts the deltas and priority.
    while (current != NULL
           && (local_handle_delta >= current->delta || current->delta == 0u
               || (((local_handle_delta - current->delta) == 0) && (handle->priority > current->priority)))) {
      local_handle_delta -= current->delta;
      handle->delta = local_handle_delta;
      prev = current;
      current = current->next;
    }

    // Insert timer in middle of delta list.
    if (prev != NULL) {
      prev->next = handle;
    } else {
      timer_head = handle;
    }
    handle->next = current;

    if (current != NULL) {
      current->delta -= local_handle_delta;
    }
  } else {
    timer_head = handle;
    handle->next = NULL;
  }
}

/*******************************************************************************
 * Removes a timer from delta list.
 *
 * @param handle Pointer to handle to timer.
 *
 * @return 0 if successful. Error code otherwise.
 ******************************************************************************/
static sl_status_t delta_list_remove_timer(sl_sleeptimer_timer_handle_t *handle)
{
  sl_sleeptimer_timer_handle_t *prev = NULL;
  sl_sleeptimer_timer_handle_t *current = timer_head;

  // Retrieve timer in delta list.
  while (current != NULL && current != handle) {
    prev = current;
    current = current->next;
  }

  if (current != handle) {
    return SL_STATUS_INVALID_STATE;
  }

  if (prev != NULL) {
    prev->next = handle->next;
  } else {
    timer_head = handle->next;
  }

  // Update delta of next timer
  if (handle->next != NULL) {
    handle->next->delta += handle->delta;
  }

  return SL_STATUS_OK;
}

/*******************************************************************************
 * Sets comparator for next timer.
 ******************************************************************************/
static void set_comparator_for_next_timer(void)
{
  sl_sleeptimer_tick_count_t compare_value;

  compare_value = last_delta_update_count + timer_head->delta;

  sleeptimer_hal_enable_int(SLEEPTIMER_EVENT_COMP);
  sleeptimer_hal_set_compare(compare_value);
}

/*******************************************************************************
 * Updates delta of first timer.
 ******************************************************************************/
static void update_first_timer_delta(void)
{
  sl_sleeptimer_tick_count_t current_cnt = sleeptimer_hal_get_counter();
  sl_sleeptimer_tick_count_t time_diff;

  if (timer_head) {
    time_diff = current_cnt - last_delta_update_count;
    if (timer_head->delta >= time_diff) {
      timer_head->delta -= time_diff;
      last_delta_update_count = current_cnt;
    } else {
      last_delta_update_count = current_cnt - timer_head->delta;
      timer_head->delta = 0;
    }
  } else {
    last_delta_update_count = current_cnt;
  }
}

/*******************************************************************************
 * Creates and start a 32 bits timer.
 *
 * @param handle Pointer to handle to timer.
 * @param timeout_initial Initial timeout, in timer ticks.
 * @param timeout_periodic Periodic timeout, in timer ticks. This timeout
 *        applies once timeoutInitial expires. Can be set to 0 for a one
 *        shot timer.
 * @param callback Callback function that will be called when
 *        initial/periodic timeout expires.
 * @param callback_data Pointer to user data that will be passed to callback.
 * @param priority Priority of callback. Useful in case multiple timer expire
 *        at the same time. 0 = highest priority.
 *
 * @return 0 if successful. Error code otherwise.
 ******************************************************************************/
static sl_status_t create_timer(sl_sleeptimer_timer_handle_t *handle,
                                sl_sleeptimer_tick_count_t timeout_initial,
                                sl_sleeptimer_tick_count_t timeout_periodic,
                                sl_sleeptimer_timer_callback_t callback,
                                void *callback_data,
                                uint8_t priority,
                                uint16_t option_flags)
{
  CORE_DECLARE_IRQ_STATE;

  handle->priority = priority;
  handle->callback_data = callback_data;
  handle->next = NULL;
  handle->timeout_periodic = timeout_periodic;
  handle->callback = callback;
  handle->option_flags = option_flags;

  if (timeout_initial == 0) {
    handle->delta = 0;
    if (handle->callback != NULL) {
      handle->callback(handle, handle->callback_data);
    }
    if (timeout_periodic != 0) {
      timeout_initial = timeout_periodic;
    } else {
      return SL_STATUS_OK;
    }
  }

  CORE_ENTER_ATOMIC();
  update_first_timer_delta();
  delta_list_insert_timer(handle, timeout_initial);

  // If first timer, update timer comparator.
  if (timer_head == handle) {
    set_comparator_for_next_timer();
  }

  CORE_EXIT_ATOMIC();

  return SL_STATUS_OK;
}

/*******************************************************************************
 * Convert dividend to logarithmic value. It only works for even
 * numbers equal to 2^n.
 *
 * @param  div An unscaled dividend.
 *
 * @return Logarithm of 2.
 ******************************************************************************/
__STATIC_INLINE uint32_t div_to_log2(uint32_t div)
{
  return 31UL - __CLZ(div);  // Count leading zeroes and "reverse" result.
}

/*******************************************************************************
 * Determines if a number is a power of two.
 *
 * @param  nbr Input value.
 *
 * @return True if the number is a power of two.
 ******************************************************************************/
__STATIC_INLINE bool is_power_of_2(uint32_t nbr)
{
  if ((((nbr) != 0u) && (((nbr) & ((nbr) - 1u)) == 0u))) {
    return true;
  } else {
    return false;
  }
}

#if SL_SLEEPTIMER_WALLCLOCK_CONFIG
/*******************************************************************************
 * Compute the day of the week.
 *
 * @param day Days since January 1st of 1970.
 *
 * @return the day of the week.
 ******************************************************************************/
static sl_sleeptimer_weekDay_t compute_day_of_week(uint32_t day)
{
  return (sl_sleeptimer_weekDay_t)((day + 4) % 7);
}

/*******************************************************************************
 * Compute the day of the year. This function assumes that the inputs are properly
 * sanitized.
 *
 * @param month Number of months since January.
 * @param day Day of the month
 * @param is_leap_year Specifies if the year computed against is a leap year.
 *
 * @return the number of days since the beginning of the year
 ******************************************************************************/
static uint16_t compute_day_of_year(sl_sleeptimer_month_t month, uint8_t day, bool is_leap_year)
{
  uint8_t i;
  uint16_t dayOfYear = 0;

  for (i = 0; i < month; ++i) {
    dayOfYear += days_in_month[is_leap_year][i];
  }
  dayOfYear += day;

  return dayOfYear;
}

/*******************************************************************************
 * Checks if the year is a leap year.
 *
 * @param year Year to check.
 *
 * @return true if the year is a leap year. False otherwise.
 ******************************************************************************/
static bool is_leap_year(uint16_t year)
{
  bool leap_year;

  leap_year = (((year %   4u) == 0u)
               && (((year % 100u) != 0u) || ((year % 400u) == 0u))) ? true : false;

  return (leap_year);
}

/*******************************************************************************
 * Checks if the time stamp, format and time zone are
 *  within the supported range.
 *
 * @param time Time stamp to check.
 * @param format Format of the time.
 * @param time_zone Time zone offset in second.
 *
 * @return true if the time is valid. False otherwise.
 ******************************************************************************/
static bool is_valid_time(sl_sleeptimer_timestamp_t time,
                          sl_sleeptimer_time_format_t format,
                          sl_sleeptimer_time_zone_offset_t time_zone)
{
  bool valid_time = false;

  // Check for overflow.
  if ((time_zone < 0 && time > (uint32_t)abs(time_zone)) \
      || (time_zone >= 0 && (time <= UINT32_MAX - time_zone))) {
    valid_time = true;
  }
  if (format == TIME_FORMAT_UNIX) {
    if (time > TIME_UNIX_TIMESTAMP_MAX) { // Check if Unix time stamp is an unsigned 31 bits.
      valid_time = false;
    }
  } else {
    if ((format == TIME_FORMAT_NTP) && (time >= TIME_NTP_EPOCH_OFFSET_SEC)) {
      valid_time &= true;
    } else if ((format == TIME_FORMAT_ZIGBEE_CLUSTER) && (time <= TIME_UNIX_TIMESTAMP_MAX - TIME_ZIGBEE_EPOCH_OFFSET_SEC)) {
      valid_time &= true;
    } else {
      valid_time = false;
    }
  }
  return (valid_time);
}

/*******************************************************************************
 * Checks if the date is valid.
 *
 * @param date Date to check.
 *
 * @return true if the date is valid. False otherwise.
 ******************************************************************************/
static bool is_valid_date(sl_sleeptimer_date_t *date)
{
  if ((date == NULL)
      || (date->year > TIME_UNIX_YEAR_MAX)
      || (date->month > MONTH_DECEMBER)
      || (date->month_day == 0 || date->month_day > days_in_month[is_leap_year(date->year)][date->month])
      || (date->hour > 23)
      || (date->min > 59)
      || (date->sec > 59)) {
    return false;
  }

  //Unix is valid until the 19th of January 2038 at 03:14:07
  if (date->year == TIME_UNIX_YEAR_MAX) {
    if ((uint8_t)date->month > (uint8_t)MONTH_JANUARY) {
      return false;
    } else if (date->month_day > 19) {
      return false;
    } else if (date->hour > 3) {
      return false;
    } else if (date->min > 14) {
      return false;
    } else if (date->sec > 7) {
      return false;
    }
  }

  return true;
}
#endif

/// @endcond

/* *INDENT-OFF* */
/* THE REST OF THE FILE IS DOCUMENTATION ONLY! */
/// @addtogroup platform_service
/// @{
/// @addtogroup SLEEPTIMER
/// @brief sleeptimer module
/// @{
///
///   @details
///   The sleeptimer.c and sleeptimer.h source files for the SLEEPTIMER device driver library are in the
///   service/sleeptimer folder.
///
///   @li @ref sleeptimer_intro
///   @li @ref sleeptimer_functionalities_overview
///   @li @ref sleeptimer_conf
///   @li @ref sleeptimer_api
///   @li @ref sleeptimer_example
///
///   @n @section sleeptimer_intro Introduction
///
///   The Sleeptimer driver provides software timers, delays, timekeeping and date functionalities based on the low-frequency real-time clock peripheral.
///
///   All Silicon Laboratories microcontrollers equipped with the RTC or RTCC peripheral are currently supported.
///   Only one instance of this driver can be initialized by the application.
///
///   @n @section sleeptimer_functionalities_overview Functionalities overview
///
///   @n @subsection software_timers Software timers
///
///   This functionality allows the user to create periodic and one shot timers. A user callback can be associated with a timer. It will be called from when the timer expires.
///
///   Timer structures must be allocated by the user.
///   The callback function is called from within an interrupt handler with interrupts enabled.
///
///   @n @subsection timekeeping Timekeeping
///
///   A 64-bits tick counter accessible through the uint64_t sl_sleeptimer_get_tick_count64(void) API.
///   It keeps the tick count since the initialization of the driver
///
///   The SL_SLEEPTIMER_WALLCLOCK_CONFIG configuration enables a UNIX timestamp (seconds count since January 1, 1970, 00:00:00).
///
///   This timestamp can also be accessed the following API:
///
///   @li sl_sleeptimer_timestamp_t sl_sleeptimer_get_time(void);
///   @li sl_status_t sl_sleeptimer_set_time(sl_sleeptimer_timestamp_t time);
///
///   Convenience conversion functions are provided to convert UNIX timestamp to NTP and Zigbee cluster format :
///
///   @li sl_status_t sl_sleeptimer_convert_unix_time_to_ntp(sl_sleeptimer_timestamp_t time, uint32_t *ntp_time);
///   @li sl_status_t sl_sleeptimer_convert_ntp_time_to_unix(uint32_t ntp_time, sl_sleeptimer_timestamp_t *time);
///   @li sl_status_t sl_sleeptimer_convert_unix_time_to_zigbee(sl_sleeptimer_timestamp_t time, uint32_t *zigbee_time);
///   @li sl_status_t sl_sleeptimer_convert_zigbee_time_to_unix(uint32_t zigbee_time, sl_sleeptimer_timestamp_t *time);
///
///   @n @subsection date Date
///
///   The previously described internal timestamp can also be accessed through a date format sl_sleeptimer_date_t.
///
///   @n <b>API :</b> @n
///
///   @li sl_status_t sl_sleeptimer_get_datetime(sl_sleeptimer_date_t *date);
///   @li sl_status_t sl_sleeptimer_set_datetime(sl_sleeptimer_date_t *date);
///
///   @n @subsection frequency_setup Frequency setup and tick unit
///
///   This driver works with a configurable time unit called tick.
///
///   The value of a tick is based on the clock source and the internal frequency divider.
///
///   One of the following clock sources must be enabled before initializing the sleeptimer:
///
///   @li LFXO: external crystal oscillator.  Typically running at 32.768 kHz.
///   @li LFRCO: internal oscillator running at 32.768 kHz
///   @li ULFRCO: Ultra low-frequency oscillator running at 1.000 kHz
///
///   The frequency divider is selected with the SL_SLEEPTIMER_FREQ_DIVIDER configuration. Its value must be a power of two within the range 1 to 32.
///
///   Tick (seconds) = 1 / (clock_frequency / frequency_divider)
///
///   The highest resolution is 30.5 us. It is achieved with a 32.768 kHz clock and a divider of 1.
///
///   @n @section sleeptimer_conf Configuration Options
///
///   SL_SLEEPTIMER_PERIPHERAL can be set to one of the three following values :
///
///   @code{.c}
///
///   #define SL_SLEEPTIMER_PERIPHERAL_DEFAULT 0
///   #define SL_SLEEPTIMER_PERIPHERAL_RTCC    1
///   #define SL_SLEEPTIMER_PERIPHERAL_RTC     2
///
///   // SL_SLEEPTIMER_PERIPHERAL Timer Peripheral Used by Sleeptimer
///   // SL_SLEEPTIMER_PERIPHERAL_DEFAULT = Default (auto select)
///   // SL_SLEEPTIMER_PERIPHERAL_RTCC = RTCC
///   // SL_SLEEPTIMER_PERIPHERAL_RTC = RTC
///   // Selection of the Timer Peripheral Used by the Sleeptimer
///   #define SL_SLEEPTIMER_PERIPHERAL  SL_SLEEPTIMER_PERIPHERAL_DEFAULT
///
///   @endcode
///
///   SL_SLEEPTIMER_WALLCLOCK_CONFIG must be defined to 1 to enable the following functionalities :
///
///   @li Timekeeping through UNIX timestamp
///   @li Date
///   @li Timestamp  Conversion functions
///
///   @code{.c}
///
///   // SL_SLEEPTIMER_WALLCLOCK_CONFIG Enable wallclock functionality
///   // Enable or disable wallclock functionalities (get_time, get_date, etc).
///   // Default: 0
///   #define SL_SLEEPTIMER_WALLCLOCK_CONFIG  0
///
///   // SL_SLEEPTIMER_FREQ_DIVIDER> Timer frequency divider
///   // Default: 1
///   #define SL_SLEEPTIMER_FREQ_DIVIDER  1
///
///   @endcode
///
///   SL_SLEEPTIMER_FREQ_DIVIDER value must be a power of 2 within the range 1 to 32.
///
///   @n @section sleeptimer_api The API
///
///   This section contains brief descriptions of the API functions. For
///   more information about input and output parameters and return values,
///   click on the hyperlinked function names. Most functions return an error
///   code, @ref SL_STATUS_OK is returned on success,
///   see @ref sl_status.h for other error codes.
///
///   The application code must include the @em sl_sleeptimer.h header file.
///
///   All API functions can be called from within interrupt handlers.
///
///   @ref sl_sleeptimer_init() @n
///    These functions initialize the sleeptimer driver. Typically,
///    @htmlonly sl_sleeptimer_init() @endhtmlonly is called once in the startup code.
///
///   @ref sl_sleeptimer_start_timer() @n
///    Start a one shot 32 bits timer. When a timer expires, a user-supplied callback function
///    is called. A pointer to this function is passed to
///    @htmlonly sl_sleeptimer_start_timer()@endhtmlonly. See @ref callback for
///    details of the callback prototype.
///
///   @ref sl_sleeptimer_restart_timer() @n
///    Restart a one shot 32 bits timer. When a timer expires, a user-supplied callback function
///    is called. A pointer to this function is passed to
///    @htmlonly sl_sleeptimer_start_timer()@endhtmlonly. See @ref callback for
///    details of the callback prototype.
///
///   @ref sl_sleeptimer_start_periodic_timer() @n
///    Start a periodic 32 bits timer. When a timer expires, a user-supplied callback function
///    is called. A pointer to this function is passed to
///    @htmlonly sl_sleeptimer_start_timer()@endhtmlonly. See @ref callback for
///    details of the callback prototype.
///
///   @ref sl_sleeptimer_restart_periodic_timer() @n
///    Restart a periodic 32 bits timer. When a timer expires, a user-supplied callback function
///    is called. A pointer to this function is passed to
///    @htmlonly sl_sleeptimer_start_timer()@endhtmlonly. See @ref callback for
///    details of the callback prototype.
///
///   @ref sl_sleeptimer_stop_timer() @n
///    Stop a timer.
///
///   @ref sl_sleeptimer_get_timer_time_remaining() @n
///    Get time left to the timer expiration.
///
///   @ref sl_sleeptimer_delay_millisecond() @n
///    Delay for the given number of milliseconds. This is an "active wait" delay function.
///
///   @ref sl_sleeptimer_is_timer_running() @n
///    Check if a timer is running.
///
///   @ref sl_sleeptimer_get_time(), @ref sl_sleeptimer_set_time() @n
///    Get or set wallclock time.
///
///   @ref sl_sleeptimer_ms_to_tick(), @ref sl_sleeptimer_ms32_to_tick(),
///   @ref sl_sleeptimer_tick_to_ms(), @ref sl_sleeptimer_tick64_to_ms() @n
///    Convert between milliseconds and RTC/RTCC
///    counter ticks.
///
///   @n @anchor callback <b>The timer expiry callback function:</b> @n
///   The callback function, prototyped as @ref sl_sleeptimer_timer_callback_t(), is called from
///   within the RTC peripheral interrupt handler on timer expiration.
///   @htmlonly sl_sleeptimer_timer_callback_t(sl_sleeptimer_timer_handle_t *handle, void *data)@endhtmlonly
///
///   @n @section sleeptimer_example Example
///   @code{.c}
///#include "sl_sleeptimer.h"
///
///void my_timer_callback(sl_sleeptimer_timer_handle_t *handle, void *data)
///{
///  //Code executed when the timer expire.
///}
///
///int main(void)
///{
///  sl_status_t status;
///  sl_sleeptimer_timer_handle_t my_timer;
///  uint32_t timer_timeout = 300;
///
///  CMU_ClockSelectSet(cmuClock_LFE, cmuSelect_LFRCO);
///  CMU_ClockEnable(cmuClock_RTCC, true);
///
///  status = sl_sleeptimer_init();
///  if(status != SL_STATUS_OK) {
///    printf("Sleeptimer init error.\r\n");
///  }
///
///  status = sl_sleeptimer_start_timer(&my_timer,
///                                     timer_timeout,
///                                     my_timer_callback,
///                                     (void *)NULL,
///                                     0
///                                     0);
///  if(status != SL_STATUS_OK) {
///    printf("Timer not started.\r\n");
///  }
///
///  while(1) {
///  }
///
///  return 0;
///}
///   @endcode
///
/// @} (end addtogroup SLEEPTIMER)
/// @} (end addtogroup platform_service)
