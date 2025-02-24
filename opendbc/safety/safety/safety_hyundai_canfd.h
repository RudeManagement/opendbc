#pragma once

#include "safety_declarations.h"
#include "safety_hyundai_common.h"

// *** Addresses checked in rx hook ***
// EV, ICE, HYBRID: ACCELERATOR (0x35), ACCELERATOR_BRAKE_ALT (0x100), ACCELERATOR_ALT (0x105)
#define HYUNDAI_CANFD_COMMON_RX_CHECKS(pt_bus)                                                                      \
  {.msg = {{0x35, (pt_bus), 32, .check_checksum = true, .max_counter = 0xffU, .frequency = 100U},                   \
           {0x100, (pt_bus), 32, .check_checksum = true, .max_counter = 0xffU, .frequency = 100U},                  \
           {0x105, (pt_bus), 32, .check_checksum = true, .max_counter = 0xffU, .frequency = 100U}}},                \
  {.msg = {{0x175, (pt_bus), 24, .check_checksum = true, .max_counter = 0xffU, .frequency = 50U}, { 0 }, { 0 }}},   \
  {.msg = {{0xa0, (pt_bus), 24, .check_checksum = true, .max_counter = 0xffU, .frequency = 100U}, { 0 }, { 0 }}},   \
  {.msg = {{0xea, (pt_bus), 24, .check_checksum = true, .max_counter = 0xffU, .frequency = 100U}, { 0 }, { 0 }}},   \
  {.msg = {{0x1cf, (pt_bus), 8, .check_checksum = false, .max_counter = 0xfU, .frequency = 50U},                    \
           {0x1aa, (pt_bus), 16, .check_checksum = false, .max_counter = 0xffU, .frequency = 50U}, { 0 }}},         \
  {.msg = {{0x125, (pt_bus), 16, .check_checksum = true, .max_counter = 0xffU, .frequency = 100U}, { 0 }, { 0 }}},  \

// SCC_CONTROL (from ADAS unit or camera)
#define HYUNDAI_CANFD_SCC_ADDR_CHECK(scc_bus)                                                                      \
  {.msg = {{0x1a0, (scc_bus), 32, .check_checksum = true, .max_counter = 0xffU, .frequency = 50U}, { 0 }, { 0 }}}, \

static bool hyundai_canfd_alt_buttons = false;
static bool hyundai_canfd_lka_steering_alt = false;
static bool hyundai_canfd_angle_steering = false;

static int hyundai_canfd_get_lka_addr(void) {
  return hyundai_canfd_lka_steering_alt ? 0x110 : 0x50;
}

static uint8_t hyundai_canfd_get_counter(const CANPacket_t *to_push) {
  uint8_t ret = 0;
  if (GET_LEN(to_push) == 8U) {
    ret = GET_BYTE(to_push, 1) >> 4;
  } else {
    ret = GET_BYTE(to_push, 2);
  }
  return ret;
}

static uint32_t hyundai_canfd_get_checksum(const CANPacket_t *to_push) {
  uint32_t chksum = GET_BYTE(to_push, 0) | (GET_BYTE(to_push, 1) << 8);
  return chksum;
}

static void hyundai_canfd_rx_hook(const CANPacket_t *to_push) {
  int bus = GET_BUS(to_push);
  int addr = GET_ADDR(to_push);

  const int pt_bus = hyundai_canfd_lka_steering ? 1 : 0;
  const int scc_bus = hyundai_camera_scc ? 2 : pt_bus;

  if (bus == pt_bus) {
    if (addr == 0xea) {
      // driver torque
      int torque_driver_new = ((GET_BYTE(to_push, 11) & 0x1fU) << 8U) | GET_BYTE(to_push, 10);
      torque_driver_new -= 4095;
      update_sample(&torque_driver, torque_driver_new);
    }

    // steering angle
    if (addr == 0x125) {
      int angle_meas_new = ((GET_BYTE(to_push, 3) << 8) | GET_BYTE(to_push, 4));
      // Multiply by -10 to apply the DBC scaling factor of -0.1 for STEERING_ANGLE
      angle_meas_new = to_signed(angle_meas_new, 16) * -10;
      update_sample(&angle_meas, angle_meas_new);
    }

    // cruise buttons
    const int button_addr = hyundai_canfd_alt_buttons ? 0x1aa : 0x1cf;
    if (addr == button_addr) {
      bool main_button = false;
      int cruise_button = 0;
      if (addr == 0x1cf) {
        cruise_button = GET_BYTE(to_push, 2) & 0x7U;
        main_button = GET_BIT(to_push, 19U);
      } else {
        cruise_button = (GET_BYTE(to_push, 4) >> 4) & 0x7U;
        main_button = GET_BIT(to_push, 34U);
      }
      hyundai_common_cruise_buttons_check(cruise_button, main_button);
    }

    // gas press, different for EV, hybrid, and ICE models
    if ((addr == 0x35) && hyundai_ev_gas_signal) {
      gas_pressed = GET_BYTE(to_push, 5) != 0U;
    } else if ((addr == 0x105) && hyundai_hybrid_gas_signal) {
      gas_pressed = GET_BIT(to_push, 103U) || (GET_BYTE(to_push, 13) != 0U) || GET_BIT(to_push, 112U);
    } else if ((addr == 0x100) && !hyundai_ev_gas_signal && !hyundai_hybrid_gas_signal) {
      gas_pressed = GET_BIT(to_push, 176U);
    } else {
    }

    // brake press
    if (addr == 0x175) {
      brake_pressed = GET_BIT(to_push, 81U);
    }

    // vehicle moving
    if (addr == 0xa0) {
      uint32_t front_left_speed = GET_BYTES(to_push, 8, 2);
      uint32_t rear_right_speed = GET_BYTES(to_push, 14, 2);
      vehicle_moving = (front_left_speed > HYUNDAI_STANDSTILL_THRSLD) || (rear_right_speed > HYUNDAI_STANDSTILL_THRSLD);
    }
  }

  if (bus == scc_bus) {
    // cruise state
    if ((addr == 0x1a0) && !hyundai_longitudinal) {
      // 1=enabled, 2=driver override
      int cruise_status = ((GET_BYTE(to_push, 8) >> 4) & 0x7U);
      bool cruise_engaged = (cruise_status == 1) || (cruise_status == 2);
      hyundai_common_cruise_state_check(cruise_engaged);
    }
  }

  const int steer_addr = hyundai_canfd_lka_steering ? hyundai_canfd_get_lka_addr() : 0x12a;
  bool stock_ecu_detected = (addr == steer_addr) && (bus == 0);
  if (hyundai_longitudinal) {
    // on LKA steering cars, ensure ADRV ECU is still knocked out
    // on others, ensure accel msg is blocked from camera
    const int stock_scc_bus = hyundai_canfd_lka_steering ? 1 : 0;
    stock_ecu_detected = stock_ecu_detected || ((addr == 0x1a0) && (bus == stock_scc_bus));
  }
  generic_rx_checks(stock_ecu_detected);

}

static bool hyundai_canfd_tx_hook(const CANPacket_t *to_send) {
  const SteeringLimits HYUNDAI_CANFD_STEERING_LIMITS = {
    .max_steer = 270,
    .max_rt_delta = 112,
    .max_rt_interval = 250000,
    .max_rate_up = 2,
    .max_rate_down = 3,
    .driver_torque_allowance = 250,
    .driver_torque_factor = 2,
    .type = TorqueDriverLimited,

    // the EPS faults when the steering angle is above a certain threshold for too long. to prevent this,
    // we allow setting torque actuation bit to 0 while maintaining the requested torque value for two consecutive frames
    .min_valid_request_frames = 89,
    .max_invalid_request_frames = 2,
    .min_valid_request_rt_interval = 810000,  // 810ms; a ~10% buffer on cutting every 90 frames
    .has_steer_req_tolerance = true,

    .angle_deg_to_can = 10,
    .angle_rate_up_lookup = {
      {0., 5., 25.},
      {2.5, 1.5, 0.2}
    },
    .angle_rate_down_lookup = {
      {0., 5., 25.},
      {5., 2.0, 0.3}
    },
  };

  bool tx = true;
  int addr = GET_ADDR(to_send);

  // steering
  const int steer_addr = (hyundai_canfd_lka_steering && !hyundai_longitudinal) ? hyundai_canfd_get_lka_addr() : 0x12a;
  if (addr == steer_addr) {
    if (hyundai_canfd_angle_steering) {
      int lka_active_angle = (GET_BYTE(to_send, 9) >> 5) & 0x3U;
      bool steer_angle_req = (lka_active_angle == 2U);

      int desired_angle = (((GET_BYTE(to_send, 10) & 0x3FU) << 8) | GET_BYTE(to_send, 11));
      // Multiply by -10 to apply the DBC scaling factor of -0.1 for LKAS_ANGLE_CMD
      desired_angle = to_signed(desired_angle, 14) * -HYUNDAI_CANFD_STEERING_LIMITS.angle_deg_to_can;

      if (steer_angle_cmd_checks(desired_angle, steer_angle_req, HYUNDAI_CANFD_STEERING_LIMITS)) {
        tx = false;
      }
    } else {
      int desired_torque = (((GET_BYTE(to_send, 6) & 0xFU) << 7U) | (GET_BYTE(to_send, 5) >> 1U)) - 1024U;
      bool steer_req = GET_BIT(to_send, 52U);

      if (steer_torque_cmd_checks(desired_torque, steer_req, HYUNDAI_CANFD_STEERING_LIMITS)) {
        tx = false;
      }
    }
  }

  // cruise buttons check
  if (addr == 0x1cf) {
    int button = GET_BYTE(to_send, 2) & 0x7U;
    bool is_cancel = (button == HYUNDAI_BTN_CANCEL);
    bool is_resume = (button == HYUNDAI_BTN_RESUME);

    bool allowed = (is_cancel && cruise_engaged_prev) || (is_resume && controls_allowed);
    if (!allowed) {
      tx = false;
    }
  }

  // UDS: only tester present ("\x02\x3E\x80\x00\x00\x00\x00\x00") allowed on diagnostics address
  if ((addr == 0x730) && hyundai_canfd_lka_steering) {
    if ((GET_BYTES(to_send, 0, 4) != 0x00803E02U) || (GET_BYTES(to_send, 4, 4) != 0x0U)) {
      tx = false;
    }
  }

  // ACCEL: safety check
  if (addr == 0x1a0) {
    int desired_accel_raw = (((GET_BYTE(to_send, 17) & 0x7U) << 8) | GET_BYTE(to_send, 16)) - 1023U;
    int desired_accel_val = ((GET_BYTE(to_send, 18) << 4) | (GET_BYTE(to_send, 17) >> 4)) - 1023U;

    bool violation = false;

    if (hyundai_longitudinal) {
      violation |= longitudinal_accel_checks(desired_accel_raw, HYUNDAI_LONG_LIMITS);
      violation |= longitudinal_accel_checks(desired_accel_val, HYUNDAI_LONG_LIMITS);
    } else {
      // only used to cancel on here
      if ((desired_accel_raw != 0) || (desired_accel_val != 0)) {
        violation = true;
      }
    }

    if (violation) {
      tx = false;
    }
  }

  return tx;
}

static int hyundai_canfd_fwd_hook(int bus_num, int addr) {
  int bus_fwd = -1;

  if (bus_num == 0) {
    bus_fwd = 2;
  }
  if (bus_num == 2) {
    // LKAS for cars with LKAS and LFA messages, LFA for cars with no LKAS messages
    int lfa_block_addr = hyundai_canfd_lka_steering_alt ? 0x362 : 0x2a4;
    bool is_lka_msg = ((addr == hyundai_canfd_get_lka_addr()) || (addr == lfa_block_addr)) && hyundai_canfd_lka_steering;
    bool is_lfa_msg = ((addr == 0x12a) && !hyundai_canfd_lka_steering);

    // HUD icons
    bool is_lfahda_msg = ((addr == 0x1e0) && !hyundai_canfd_lka_steering);

    // SCC_CONTROL for camera SCC cars, we send our own longitudinal commands
    bool is_scc_msg = ((addr == 0x1a0) && hyundai_longitudinal && !hyundai_canfd_lka_steering);

    bool block_msg = is_lka_msg || is_lfa_msg || is_lfahda_msg || is_scc_msg;
    if (!block_msg) {
      bus_fwd = 0;
    }
  }

  return bus_fwd;
}

static safety_config hyundai_canfd_init(uint16_t param) {
  const int HYUNDAI_PARAM_CANFD_LKA_STEERING_ALT = 128;
  const int HYUNDAI_PARAM_CANFD_ALT_BUTTONS = 32;
  const int HYUNDAI_PARAM_CANFD_ANGLE_STEERING = 256;

  static const CanMsg HYUNDAI_CANFD_LKA_STEERING_TX_MSGS[] = {
    {0x50, 0, 16},  // LKAS
    {0x1CF, 1, 8},  // CRUISE_BUTTON
    {0x2A4, 0, 24}, // CAM_0x2A4
  };

  static const CanMsg HYUNDAI_CANFD_LKA_STEERING_ALT_TX_MSGS[] = {
    {0x110, 0, 32}, // LKAS_ALT
    {0x1CF, 1, 8},  // CRUISE_BUTTON
    {0x362, 0, 32}, // CAM_0x362
  };

  static const CanMsg HYUNDAI_CANFD_LKA_STEERING_LONG_TX_MSGS[] = {
    {0x50, 0, 16},  // LKAS
    {0x1CF, 1, 8},  // CRUISE_BUTTON
    {0x2A4, 0, 24}, // CAM_0x2A4
    {0x51, 0, 32},  // ADRV_0x51
    {0x730, 1, 8},  // tester present for ADAS ECU disable
    {0x12A, 1, 16}, // LFA
    {0x160, 1, 16}, // ADRV_0x160
    {0x1E0, 1, 16}, // LFAHDA_CLUSTER
    {0x1A0, 1, 32}, // CRUISE_INFO
    {0x1EA, 1, 32}, // ADRV_0x1ea
    {0x200, 1, 8},  // ADRV_0x200
    {0x345, 1, 8},  // ADRV_0x345
    {0x1DA, 1, 32}, // ADRV_0x1da
  };

  static const CanMsg HYUNDAI_CANFD_LFA_STEERING_TX_MSGS[] = {
    {0x12A, 0, 16}, // LFA
    {0x1A0, 0, 32}, // CRUISE_INFO
    {0x1CF, 2, 8},  // CRUISE_BUTTON
    {0x1E0, 0, 16}, // LFAHDA_CLUSTER
  };


  hyundai_common_init(param);

  gen_crc_lookup_table_16(0x1021, hyundai_canfd_crc_lut);
  hyundai_canfd_alt_buttons = GET_FLAG(param, HYUNDAI_PARAM_CANFD_ALT_BUTTONS);
  hyundai_canfd_lka_steering_alt = GET_FLAG(param, HYUNDAI_PARAM_CANFD_LKA_STEERING_ALT);
  hyundai_canfd_angle_steering = GET_FLAG(param, HYUNDAI_PARAM_CANFD_ANGLE_STEERING);

  // no long for radar-SCC with LFA steering yet
  if (!hyundai_canfd_lka_steering && !hyundai_camera_scc) {
    hyundai_longitudinal = false;
  }

  safety_config ret;
  if (hyundai_longitudinal) {
    if (hyundai_canfd_lka_steering) {
      static RxCheck hyundai_canfd_lka_steering_long_rx_checks[] = {
        HYUNDAI_CANFD_COMMON_RX_CHECKS(1)
      };

      ret = BUILD_SAFETY_CFG(hyundai_canfd_lka_steering_long_rx_checks, HYUNDAI_CANFD_LKA_STEERING_LONG_TX_MSGS);
    } else {
      // Longitudinal checks for LFA steering
      static RxCheck hyundai_canfd_long_rx_checks[] = {
        HYUNDAI_CANFD_COMMON_RX_CHECKS(0)
      };

      ret = BUILD_SAFETY_CFG(hyundai_canfd_long_rx_checks, HYUNDAI_CANFD_LFA_STEERING_TX_MSGS);
    }
  } else {
    if (hyundai_canfd_lka_steering) {
      // *** LKA steering checks ***
      // E-CAN is on bus 1, SCC messages are sent on cars with ADRV ECU.
      // Does not use the alt buttons message
      static RxCheck hyundai_canfd_lka_steering_rx_checks[] = {
        HYUNDAI_CANFD_COMMON_RX_CHECKS(1)
        HYUNDAI_CANFD_SCC_ADDR_CHECK(1)
      };

      ret = hyundai_canfd_lka_steering_alt ? BUILD_SAFETY_CFG(hyundai_canfd_lka_steering_rx_checks, HYUNDAI_CANFD_LKA_STEERING_ALT_TX_MSGS) : \
                                              BUILD_SAFETY_CFG(hyundai_canfd_lka_steering_rx_checks, HYUNDAI_CANFD_LKA_STEERING_TX_MSGS);
    } else if (!hyundai_camera_scc) {
      // Radar sends SCC messages on these cars instead of camera
      static RxCheck hyundai_canfd_radar_scc_rx_checks[] = {
        HYUNDAI_CANFD_COMMON_RX_CHECKS(0)
        HYUNDAI_CANFD_SCC_ADDR_CHECK(0)
      };

      ret = BUILD_SAFETY_CFG(hyundai_canfd_radar_scc_rx_checks, HYUNDAI_CANFD_LFA_STEERING_TX_MSGS);
    } else {
      // *** LFA steering checks ***
      // Camera sends SCC messages on LFA steering cars.
      // Both button messages exist on some platforms, so we ensure we track the correct one using flag
      static RxCheck hyundai_canfd_rx_checks[] = {
        HYUNDAI_CANFD_COMMON_RX_CHECKS(0)
        HYUNDAI_CANFD_SCC_ADDR_CHECK(2)
      };

      ret = BUILD_SAFETY_CFG(hyundai_canfd_rx_checks, HYUNDAI_CANFD_LFA_STEERING_TX_MSGS);
    }
  }

  return ret;
}

const safety_hooks hyundai_canfd_hooks = {
  .init = hyundai_canfd_init,
  .rx = hyundai_canfd_rx_hook,
  .tx = hyundai_canfd_tx_hook,
  .fwd = hyundai_canfd_fwd_hook,
  .get_counter = hyundai_canfd_get_counter,
  .get_checksum = hyundai_canfd_get_checksum,
  .compute_checksum = hyundai_common_canfd_compute_checksum,
};
