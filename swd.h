#ifndef RASBERRY_PINE_SWD_H
#define RASBERRY_PINE_SWD_H
#include <inttypes.h>
#include "common_utils.h"

#define ACK_OK 0b001
#define ACK_WAIT 0b010
#define ACK_FAULT 0b100

enum SWD_ERROR {
    SWD_OK = 0,
    SWD_ACK_WAIT,
    SWD_ACK_FAULT,
    SWD_ACK_UNKNOWN,
    SWD_PARITY_MISMATCH
};

#define SWD_DPIDR_ADDR 0x0
#define SWD_ABORT_ADDR 0x0
#define SWD_CTRLSTAT_ADDR 0x4
#define SWD_SELECT_ADDR 0x8
#define SWD_RDBUFF_ADDR 0xC
#define SWD_JTAG_TO_SWD_SEQ  0xE79E // See page 5-111 of ADIv5 Spec
#define SWD_JTAG_TO_SWD_SEQ_LEN 16
#define SWD_SWD_TO_JTAG_SEQ 0xE73C // See page 5-111 of ADIv5 Spec
#define SWD_SWD_TO_JTAG_SEQ_LEN 16

// MEM-AP stuff (todo need to make MEM-AP accesses and such its own module probably)
#define CSW_OFFSET 0x0
#define TAR_OFFSET 0x4
#define DRW_OFFSET 0xC
#define NVMC_OFFSET 0x4001E000
#define NVMC_CONFIG_OFFSET 0x504
#define NVMC_ERASEALL 0x50C

typedef struct SWD_DPIDR_Reg {
    uint32_t revision;
    uint32_t part_number;
    uint32_t min;
    uint32_t version;
    uint32_t designer;
} DPIDR_Reg;

typedef struct SWD_ABORT_Reg {
    int ORUNERRCLR;
    int WDERRCLR;
    int SKERRCLR;
    int STKCMPCLR;
    int DAPABORT;
} SWD_ABORT_Reg;

typedef struct SWD_Header {
    int APnDP;
    int RnW;
    uint8_t addr;
} SWD_Header;

typedef struct SWD_CNTRL_STAT_Reg {
    int CSYSPWRUPACK;
    int CSYSPWRUPREQ;
    int CDBGPWRUOACK;
    int CDBGPWRUPREQ;
    int CDBGRSTACK;
    int CDBGRSTREQ;
    int TRNCNT;
    int MASKLANE;
    int WDATAERR;
    int READOK;
    int STICKYERR;
    int STICKYCMP;
    int TRNMODE;
    int STICKYORUN;
    int ORUNDETECT;
} SWD_CNTRL_STAT_Reg;

typedef struct SWD_SELECT_Reg {
    uint8_t APSEL;
    uint8_t APBANKSEL;
    uint8_t DPBANKSEL;
} SWD_SELECT_Reg;

typedef struct SWD_APIDRCode {
    uint8_t revision;
    uint8_t jedec_code;
    uint8_t jedec_code_cont;
    uint8_t apid;
    uint8_t ap_class;
} SWD_APIDRCode;

typedef struct MEM_AP_CSW_Reg {
    uint8_t debug_sw_enable;
    uint8_t prot;
    uint8_t spi_debug_enable;
    uint8_t type;
    uint8_t mode;
    uint8_t transfer_in_progress;
    uint8_t device_enable;
    uint8_t addr_increment;
    uint8_t size;
} MEM_AP_CSW_Reg;


typedef struct SWD_Packet {
    SWD_Header header;
    char* debug_string;
    uint8_t ack;
    uint32_t data;
    uint32_t parity;
} SWD_Packet;

SWD_Packet swd_read_dpidr_reg();
SWD_Packet swd_read_cntrl_stat_reg();
SWD_Packet swd_write_cntrl_stat_reg(SWD_CNTRL_STAT_Reg reg);
SWD_Packet swd_write_select_reg(SWD_SELECT_Reg reg_values);
SWD_Packet swd_read_ap_idcode();
SWD_Packet swd_read_readbuff();
SWD_Packet swd_ap_write_eraseall();
SWD_Packet swd_read_erase_status();
SWD_Packet swd_read_protect_status_reg();
SWD_Packet swd_write_abort_reg(SWD_ABORT_Reg reg);
SWD_Packet swd_write_csw_reg(MEM_AP_CSW_Reg values);
SWD_Packet swd_read_ap_addr(uint8_t addr);
SWD_Packet swd_write_ap_addr(uint8_t addr, uint32_t data);


SPI_Data swd_jtag_to_swd();
SPI_Data swd_protocol_reset();
DPIDR_Reg interpret_dp_idr_reg(uint32_t word);
SWD_APIDRCode interpret_ap_idr_code(uint32_t word);
MEM_AP_CSW_Reg interpret_ap_csw_reg(uint32_t word);
SWD_CNTRL_STAT_Reg interpret_ctrlstat_reg(uint32_t word);
uint8_t create_header_word(SWD_Header header_values);

#endif
