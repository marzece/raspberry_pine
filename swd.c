#include "swd.h"
#include "common_utils.h"

uint8_t create_header_word(SWD_Header header_values) {
    uint8_t word = 0x81; // Start bit and park bit
    int parity_bit;
    word |= header_values.APnDP ? (1<<1) : 0;
    word |= header_values.RnW ? (1<<2) : 0;
    word |= ((header_values.addr>>2) & 0x3) << 3;

    // Parity bit is determined from the APnDP, RnW and addr bits.
    parity_bit = has_even_parity(word & 0x1E, 8);
    word |= parity_bit ? 0 :  1<<5;
    return word;
}

DPIDR_Reg interpret_dp_idr_reg(uint32_t word) {
    // See page 2-54 of ARM debug interface specification for origin of these bits
    DPIDR_Reg ret;
    ret.revision = word >> 28;
    ret.part_number = (word >> 20) & 0xFF;
    ret.min =  (word >> 16) & 0x1;
    ret.version = (word >> 12) & 0xF;
    ret.designer =  (word >> 1) & 0x7FF;
    return ret;
}

SWD_CNTRL_STAT_Reg interpret_ctrlstat_reg(uint32_t word) {
    // See page 2-54 of ARM debug interface specification for origin of these bits
    SWD_CNTRL_STAT_Reg ret;
    ret.CSYSPWRUPACK = (word >> 31) & 0x1;
    ret.CSYSPWRUPREQ = (word >> 30) & 0x1;
    ret.CDBGPWRUOACK =  (word >> 29) & 0x1;
    ret.CDBGPWRUPREQ =  (word >> 28) & 0x1;
    ret.CDBGRSTACK =  (word >> 27) & 0x1;
    ret.CDBGRSTREQ =  (word >> 26) & 0x1;
    ret.TRNCNT =  (word >> 12) & 0xFFF;
    ret.MASKLANE =  (word >> 8) & 0xF;
    ret.WDATAERR =  (word >> 7) & 0x1;
    ret.READOK =  (word >> 6) & 0x1;
    ret.STICKYERR =  (word >> 5) & 0x1;
    ret.STICKYCMP =  (word >> 4) & 0x1;
    ret.TRNMODE =  (word >> 2) & 0x3;
    ret.STICKYORUN =  (word >> 1) & 0x1;
    ret.ORUNDETECT =  word & 0x1;
    return ret;
}

uint32_t create_ctrlstat_word(SWD_CNTRL_STAT_Reg reg) {
    uint32_t word = 0;
    word |= reg.CSYSPWRUPACK ? (1 << 31) : 0;
    word |= reg.CSYSPWRUPREQ ? (1 << 30) : 0;
    word |= reg.CDBGPWRUOACK ?  (1 << 29) : 0;
    word |= reg.CDBGPWRUPREQ ?  (1 << 28) : 0;
    word |= reg.CDBGRSTACK ?  (1 << 27) : 0;
    word |= reg.CDBGRSTREQ ?  (1 << 26) : 0;
    word |= (reg.TRNCNT & 0xFFF) << 12;
    word |= reg.MASKLANE ?  (1 << 8) : 0;
    word |= reg.WDATAERR ?  (1 << 7) : 0;
    word |= reg.READOK ?  (1 << 6) : 0;
    word |= reg.STICKYERR ?  (1 << 5) : 0;
    word |= reg.STICKYCMP ?  (1 << 4) : 0;
    word |= (reg.TRNMODE & 0x3) << 2;
    word |= reg.STICKYORUN ?  (1 << 1) : 0;
    word |= reg.ORUNDETECT ?  0x1 : 0;
    return word;
}

SPI_Data swd_protocol_reset() {
    // A line reset is at least 50 clock cycles with data at HIGH
    // followed by at least two clock cycles with it at "idle".
    // (NTS, idle here means LOW see 4.2.3 of ARM Debug Interface Spec, ADIv5)
    SPI_Data ret;
    ret.mosi[0] = 0xFFFFFF;
    ret.lengths[0] = 24;
    ret.mosi[1] = 0xFFFFFF;
    ret.lengths[1] = 24;
    ret.mosi[2] = 0xFFFFFF;
    ret.lengths[2] = 24;
    ret.mosi[3] = 0x0;
    ret.lengths[3] = 8;

    ret.n_writes = 4;
    return ret;
}

SPI_Data swd_jtag_to_swd() {
    SPI_Data ret = swd_protocol_reset();
    ret.mosi[3] = SWD_JTAG_TO_SWD_SEQ;
    ret.lengths[3] = SWD_JTAG_TO_SWD_SEQ_LEN;
    ret.n_writes=4;
    return ret;
}

SWD_Packet swd_read_dpidr_reg() {
    // Don't need to touch anything but the packet header
    SWD_Packet ret;
    SWD_Header* header = &ret.header;
    header->APnDP = 0;
    header->RnW = 1;
    header->addr = SWD_DPIDR_ADDR;
    ret.debug_string = "Read DPIDR Reg\n";
    return ret;
}

SWD_Packet swd_read_cntrl_stat_reg() {
    // Don't need to touch anything but the packet header
    SWD_Packet ret;
    SWD_Header* header = &ret.header;
    header->APnDP = 0;
    header->RnW = 1;
    header->addr = SWD_CTRLSTAT_ADDR;
    ret.debug_string = "Read CTRL/STAT Reg\n";
    return ret;
}

SWD_Packet swd_read_readbuff() {
    // Don't need to touch anything but the packet header
    SWD_Packet ret;
    SWD_Header* header = &ret.header;
    header->APnDP = 0;
    header->RnW = 1;
    header->addr = SWD_RDBUFF_ADDR;
    ret.debug_string = "Read RDBUFF Reg\n";
    return ret;
}

SWD_Packet swd_write_cntrl_stat_reg(SWD_CNTRL_STAT_Reg reg) {
    SWD_Packet ret;
    SWD_Header* header = &ret.header;
    header->APnDP = 0;
    header->RnW = 0;
    header->addr = SWD_CTRLSTAT_ADDR;
    ret.data = create_ctrlstat_word(reg);
    ret.debug_string = "Write CTRL/STAT Reg\n";
    return ret;
}

SWD_Packet swd_write_abort_reg(SWD_ABORT_Reg reg) {
    SWD_Packet ret;
    SWD_Header* header = &ret.header;
    header->APnDP = 0;
    header->RnW = 0;
    header->addr = SWD_ABORT_ADDR;
    ret.debug_string = "Write ABORT Reg\n";

    ret.data = reg.ORUNERRCLR ? 1<<4 : 0;
    ret.data |= reg.WDERRCLR  ? 1<<3 : 0;
    ret.data |= reg.SKERRCLR  ? 1<<2 : 0;
    ret.data |= reg.STKCMPCLR ? 1<<1 : 0;
    ret.data |= reg.DAPABORT  ? 1<<0 : 0;

    return ret;
}

SWD_Packet swd_write_select_reg(SWD_SELECT_Reg reg_values) {
    SWD_Packet ret;
    SWD_Header* header = &ret.header;
    header->APnDP = 0;
    header->RnW = 0;
    header->addr = SWD_SELECT_ADDR;
    ret.debug_string = "Write SELECT Reg\n";

    ret.data = (reg_values.APSEL & 0xFF) << 24;
    ret.data |= (reg_values.APBANKSEL & 0xF) << 4;
    ret.data |= reg_values.DPBANKSEL & 0xF;
    return ret;
}

SWD_Packet swd_ap_write_eraseall() {
    SWD_Packet ret;
    SWD_Header* header = &ret.header;
    header->APnDP = 1;
    header->RnW = 0;
    header->addr = 0x4;
    ret.debug_string = "Write ERASEALL Reg\n";
    ret.data = 0x1;
    return ret;
}

SWD_Packet swd_read_erase_status() {
    SWD_Packet ret;
    SWD_Header* header = &ret.header;
    header->APnDP = 1;
    header->RnW = 1;
    header->addr = 0x8;
    ret.debug_string = "READ ERASE STATUS Reg\n";
    return ret;
}

SWD_Packet swd_read_protect_status_reg() {
    SWD_Packet ret;
    SWD_Header* header = &ret.header;
    header->APnDP = 1;
    header->RnW = 1;
    header->addr = 0xC;
    ret.debug_string = "READ PROTECT STATUS Reg\n";
    return ret;
}

SWD_Packet swd_write_csw_reg(MEM_AP_CSW_Reg values) {
    SWD_Packet ret;
    SWD_Header* header = &ret.header;
    header->APnDP = 1;
    header->RnW = 0;
    header->addr = CSW_OFFSET;

    uint32_t data = 0;
    // For the NRF in particular I'm pretty sure many of the fields in the fields
    // in the upper bits of the CSW reg are not relevant b/c they're not required by
    // the SWD spec...so I don't include them here at all
    // Also lots of values are in the CSW are read only

    // I'm not totally sure what the prot does, the spec says its mostly implementation defined
    // and it resets to 0x23 and that demonstrably seems to be fine...so I'm just gonna hardcode
    // it to that so I don't have to worry about if other values of it are okay or not
    values.prot = 0x23;

    data |= (values.prot & 0x3F) << 24;
    data |= (values.mode & 0xF) << 8;
    data |= (values.addr_increment & 0x3) << 4;
    data |= (values.size & 0x7) << 0;
    ret.data = data;
    ret.debug_string = "WRITE CSW Reg\n";
    return ret;
}

SWD_Packet swd_read_ap_addr(uint8_t addr) {
    SWD_Packet ret;
    SWD_Header* header = &ret.header;
    header->APnDP = 1;
    header->RnW = 1;
    header->addr = addr;
    ret.debug_string = "READ AP ADDR Reg\n";
    return ret;
}

SWD_Packet swd_write_ap_addr(uint8_t addr, uint32_t data) {
    SWD_Packet ret;
    SWD_Header* header = &ret.header;
    header->APnDP = 1;
    header->RnW = 0;
    header->addr = addr;
    ret.data = data;
    ret.debug_string = "WRITE AP ADDR Reg\n";
    return ret;
}

SWD_APIDRCode interpret_ap_idr_code(uint32_t word) {
    SWD_APIDRCode ret;
    ret.apid = word & 0xFF;
    ret.ap_class = (word>>13) & 0xF;
    ret.jedec_code = (word >>17) & 0x3F;
    ret.jedec_code_cont = (word >> 24) & 0xF;
    ret.revision = (word >> 28) & 0xF;
    return ret;
}

MEM_AP_CSW_Reg interpret_ap_csw_reg(uint32_t word) {
    MEM_AP_CSW_Reg ret;
    ret.debug_sw_enable = (word >> 31) & 0x1;
    ret.prot = (word >> 24) & 0x3F;
    ret.spi_debug_enable = (word >> 23) & 0x1;
    ret.type = (word >> 12) & 0xF;
    ret.mode = (word >> 8) & 0xF;
    ret.transfer_in_progress = (word >> 7) & 0x1;
    ret.device_enable = (word >> 6) & 0x1;
    ret.addr_increment = (word >> 4) & 0x3;
    ret.size = word & 0x7;
    return ret;
}

// TODO, this and similar functions should just be static values or something
SWD_Packet swd_read_ap_idcode() {
    SWD_Packet ret;
    SWD_Header* header = &ret.header;
    header->APnDP = 1;
    header->RnW = 1;
    header->addr = 0xC;
    ret.debug_string = "READ AP ID Code Reg\n";
    return ret;
}
