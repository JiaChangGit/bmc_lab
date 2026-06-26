#ifndef MINI_PCIE_TELEMETRY_H
#define MINI_PCIE_TELEMETRY_H

#ifdef __KERNEL__
#include <linux/ioctl.h>
#include <linux/types.h>
typedef __u32 mini_u32;
typedef __u64 mini_u64;
#else
#include <cstdint>
#include <sys/ioctl.h>
typedef std::uint32_t mini_u32;
typedef std::uint64_t mini_u64;
#endif

#define MINI_PCIE_DEVICE_ID_SIZE 32
#define MINI_PCIE_TEXT_SIZE 16

enum mini_pcie_fault {
    MINI_PCIE_FAULT_NONE = 0,
    MINI_PCIE_FAULT_LINK_DOWN,
    MINI_PCIE_FAULT_LINK_DEGRADED,
    MINI_PCIE_FAULT_CORRECTABLE_ERROR_SPIKE,
    MINI_PCIE_FAULT_NONFATAL_ERROR,
    MINI_PCIE_FAULT_TELEMETRY_TIMEOUT,
    MINI_PCIE_FAULT_OVER_TEMPERATURE,
    MINI_PCIE_FAULT_OVER_POWER,
};

struct mini_pcie_telemetry {
    char device_id[MINI_PCIE_DEVICE_ID_SIZE];
    mini_u32 link_width;
    char link_speed[MINI_PCIE_TEXT_SIZE];
    char link_state[MINI_PCIE_TEXT_SIZE];
    mini_u32 gpu_core_temp_millic;
    mini_u32 gpu_power_milliwatt;
    mini_u64 correctable_error_count;
    mini_u64 nonfatal_error_count;
    char health[MINI_PCIE_TEXT_SIZE];
};

#define MINI_PCIE_IOC_MAGIC 'M'
#define MINI_PCIE_GET_TELEMETRY \
    _IOR(MINI_PCIE_IOC_MAGIC, 0x01, struct mini_pcie_telemetry)
#define MINI_PCIE_SET_FAULT \
    _IOW(MINI_PCIE_IOC_MAGIC, 0x02, enum mini_pcie_fault)
#define MINI_PCIE_CLEAR_FAULT _IO(MINI_PCIE_IOC_MAGIC, 0x03)
#define MINI_PCIE_INJECT_EVENT _IO(MINI_PCIE_IOC_MAGIC, 0x04)

#endif
