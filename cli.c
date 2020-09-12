#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include "linenoise/linenoise.h"
#include "rbpi.h"
#include "swd.h"
#define CSW_OFFSET 0x0
#define TAR_OFFSET 0x4
#define DRW_OFFSET 0xC
typedef int (*SWDFunc)(uint32_t* args);

typedef struct Command {
    char* name;
    int nargs;
    SWDFunc func;
} Command;

SPIRegisters spi_registers; // Global store for various RBPI SPI regs

int perform_swd_io(SPIRegisters spi_registers, SWD_Packet* packet_data) {
    /* This function uses the data in 'packet_data' to create an SPI_Data packet which
     * is then sent out to the SPI interface where the actual "on the wire" stuff happens.
     *
     * If the "packet_data" is a read operation, then the response is packed into the "data"
     * field of the packet_data. For both a read and a write operation the "ack" field
     * of packet_data is filled in.
     */

    //First thing is to send out the header and read back the response (which should include the ACK)
    uint32_t header_word = create_header_word(packet_data->header);
    int parity_bit;


    printf("Debug: %s", packet_data->debug_string);
    SPI_Data spi_data;
    spi_data.n_writes = 1;
    spi_data.mosi[0] = header_word;

    // Need to pad the header word with some extra bits so that the ACK gets sent back.
    // If this is a read operation the padding is 4 bits (1 turnaround + 3 ACK bits).
    // If this is a write operation the padding is 5 bits (1 turnaround + 3 ACK bits + 1 turnaround)
    // TODO, logically this should be done in SWD.c but that's a pain so we'll just do it here
    spi_data.mosi[0] |= 0b1111 << 8;
    spi_data.lengths[0] = 12;
    if(!packet_data->header.RnW) {
        spi_data.mosi[0] |= 1<<12;
        spi_data.lengths[0] += 1;
    }
    spi_io(spi_registers, &spi_data);
    packet_data->ack = (spi_data.miso[0] >> 9) & 0b111;


    // TODO, implement something here.
    switch(packet_data->ack) {
        case ACK_OK:
            break;
        case ACK_WAIT:
            printf("ACK_WAIT recieved\n");
            return SWD_ACK_WAIT;
        case ACK_FAULT:
            printf("ACK_FAULT recieved\n");
            return SWD_ACK_FAULT;
        default:
            printf("Invalid ACK from slave device ACK = 0x%x\n", packet_data->ack);
            //return SWD_ACK_UNKNOWN;
    }

    // If here we can continue with the transfer
    if(packet_data->header.RnW) {
        // Read data is all 1s to provide a pull-up.
        // The slave device will do the actual work
        spi_data.mosi[0] = 0xFFFF;
        spi_data.lengths[0] = 16;
        spi_data.mosi[1] = 0x1FFFF;
        spi_data.lengths[1] = 17;

    } else {
        // I could rely on the user to send in the correct parity bit...
        // but why not just do it here.
        parity_bit = has_even_parity(packet_data->data, 32) ? 0 : 1;
        spi_data.mosi[0] = packet_data->data & 0xFFFF;
        spi_data.lengths[0] = 16;
        spi_data.mosi[1] = ((packet_data->data >> 16) & 0xFFFF);
        spi_data.mosi[1] |= parity_bit ? 1 << 16 : 0; // Add the parity bit
        spi_data.lengths[1] = 17;
    }

    // Need to "close" the transaction with at least 8 "idles".
    spi_data.mosi[2] = 0x0;
    spi_data.lengths[2] = 16;
    spi_data.n_writes = 3;


    spi_io(spi_registers, &spi_data); // Send it

    // If this was a read-op then get the data back and stuff in "packet_data"
    if(packet_data->header.RnW) {
        packet_data->data = spi_data.miso[0] | (spi_data.miso[1] << 16);
        packet_data->parity = (spi_data.miso[1] >> 16) & 0x1;

        int expected_parity = !has_even_parity(packet_data->data, 32);

        if(!packet_data->parity != !expected_parity) {
            printf("Parity mismatch 0x%x %i\n", packet_data->data, packet_data->parity);
            printf("MISO = 0x%x\n", spi_data.miso[1]);
            return SWD_PARITY_MISMATCH;
        }
    }
    return SWD_OK; 
}

int jtag_to_swd(uint32_t* args) {
    SPI_Data swd_to_jtag_data = swd_jtag_to_swd();
    spi_io(spi_registers, &swd_to_jtag_data);
    return 0;
}
int swd_reset(uint32_t* args) {
    SPI_Data reset_data = swd_protocol_reset();
    spi_io(spi_registers, &reset_data);
    return 0;
}
int write_select_reg(uint32_t* args) {
    enum SWD_ERROR err;
    if(args==NULL) {
        printf("Invalid args sent to 'write_select_reg'\n");
        return 1;
    }
    SWD_SELECT_Reg select_reg = { .APSEL = args[0], .APBANKSEL = args[1], .DPBANKSEL = args[2] };
    SWD_Packet write_select_packet = swd_write_select_reg(select_reg);
    err = perform_swd_io(spi_registers, &write_select_packet);
    return err;
}

int read_protect_status(uint32_t* args) {
    SWD_Packet read_prot_status = swd_read_protect_status_reg();
    perform_swd_io(spi_registers, &read_prot_status);
    printf("PROT Status = 0x%x\n", read_prot_status.data);
    return 0;
}

int read_dpid(uint32_t* args) {
    SWD_Packet read_idr_packet = swd_read_dpidr_reg();
    perform_swd_io(spi_registers, &read_idr_packet);
    printf("IDCode = 0x%x\n", read_idr_packet.data);

    return read_idr_packet.data;
}
int read_idrcode(uint32_t* args) {

    SWD_Packet read_ap_id_packet = swd_read_ap_idcode();
    perform_swd_io(spi_registers, &read_ap_id_packet);
    printf("IDR = 0x%x\n", read_ap_id_packet.data);
    struct SWD_APIDRCode idr_reg = interpret_ap_idr_code(read_ap_id_packet.data);

    printf("Revision= 0x%x\n", idr_reg.revision);
    printf("Jedec code= 0x%x\n", idr_reg.jedec_code);
    printf("Jedec code cont= 0x%x\n", idr_reg.jedec_code_cont);
    printf("AP ID= 0x%x\n", idr_reg.apid);
    printf("AP class= 0x%x\n", idr_reg.ap_class);
    return read_ap_id_packet.data;
}

int read_ap_addr(uint32_t* args) {
    uint8_t addr = args[0];
    SWD_Packet packet = swd_read_ap_addr(addr);
    perform_swd_io(spi_registers, &packet);
    perform_swd_io(spi_registers, &packet);

    printf("Value at addr=0x%x = 0x%x\n", addr, packet.data);
    return 0;
};

int write_abort(uint32_t* args) {
    printf("NOT IMPLEMENTED!!!\n");
    return 0;
}

int read_ctrlstat(uint32_t* args) {
    SWD_Packet read_ctrlstat_reg = swd_read_cntrl_stat_reg();
    perform_swd_io(spi_registers, &read_ctrlstat_reg);
    printf("CTRL_STAT = 0x%x\n", read_ctrlstat_reg.data);
    return read_ctrlstat_reg.data;
}
int control_debug_power(uint32_t* args) {
    if(args==NULL) {
        printf("No arguements given to 'control_debug_power'\n");
        return -1;
    }

    SWD_CNTRL_STAT_Reg ctrlstat_reg;

    memset(&ctrlstat_reg, 0, sizeof(SWD_CNTRL_STAT_Reg));
    ctrlstat_reg.CSYSPWRUPREQ = args[0];
    ctrlstat_reg.CDBGPWRUPREQ = args[0];
    SWD_Packet write_cntrlstat_packet = swd_write_cntrl_stat_reg(ctrlstat_reg);
    perform_swd_io(spi_registers, &write_cntrlstat_packet);
    return 0;
}
int clear_stickyerr(uint32_t* args) {
    SWD_ABORT_Reg reg = {
     .ORUNERRCLR = 0,
     .WDERRCLR = 0,
     .SKERRCLR = 1,
     .STKCMPCLR = 0,
     .DAPABORT = 0 };

    SWD_Packet write_abort_reg = swd_write_abort_reg(reg);
    perform_swd_io(spi_registers, &write_abort_reg);

    return 0;
}

int do_erase_all(uint32_t* args) {
    SWD_Packet erase_all = swd_ap_write_eraseall();
    perform_swd_io(spi_registers, &erase_all);
    return 0;
}

int read_erase_status(uint32_t* args) {
    SWD_Packet erase_status = swd_read_erase_status();
    perform_swd_io(spi_registers, &erase_status);
    printf("Erase Status = 0x%x\n", erase_status.data);
    return 0;
}

int read_csw(uint32_t* args) {
    SWD_Packet read_csw = swd_read_ap_addr(CSW_OFFSET);
    perform_swd_io(spi_registers, &read_csw);
    perform_swd_io(spi_registers, &read_csw);

    MEM_AP_CSW_Reg csw = interpret_ap_csw_reg(read_csw.data);
    printf("MEM-AP CSW  = 0x%x\n", read_csw.data);
    printf("debug_sw_enable = 0x%x\n", csw.debug_sw_enable);
    printf("prot = 0x%x\n", csw.prot);
    printf("spi_debug_enable = 0x%x\n", csw.spi_debug_enable);
    printf("type = 0x%x\n", csw.type);
    printf("mode = 0x%x\n", csw.mode);
    printf("transfer_in_progress = 0x%x\n", csw.transfer_in_progress);
    printf("device_enable = 0x%x\n", csw.device_enable);
    printf("addr_increment = 0x%x\n", csw.addr_increment);
    printf("size = 0x%x\n", csw.size);
    return 0;
}

int read_tar(uint32_t* args) {
    SWD_Packet read_tar_reg = swd_read_ap_addr(TAR_OFFSET);
    perform_swd_io(spi_registers, &read_tar_reg);
    perform_swd_io(spi_registers, &read_tar_reg);
    printf("TAR = 0x%x\n", read_tar_reg.data);
    return read_tar_reg.data;
}

int write_tar(uint32_t* args) {
    if(args == NULL) {
        printf("No arguments given to 'write_tar'\n");
        return -1;
    }
    SWD_Packet write_tar_reg = swd_write_ap_addr(TAR_OFFSET, args[0]);
    perform_swd_io(spi_registers, &write_tar_reg);
    return 0;
}

int read_drw(uint32_t* args) {
    SWD_Packet read_drw_reg = swd_read_ap_addr(DRW_OFFSET);
    perform_swd_io(spi_registers, &read_drw_reg);
    perform_swd_io(spi_registers, &read_drw_reg);
    printf("DRW = 0x%x\n", read_drw_reg.data);
    return 0;
}

int write_drw(uint32_t* args) {
    if(args == NULL) {
        printf("No arguments given to 'write_drw'\n");
        return -1;

    }
    SWD_Packet write_drw_reg = swd_write_ap_addr(DRW_OFFSET, args[0]);
    perform_swd_io(spi_registers, &write_drw_reg);
    return 0;
}

Command commandTable[] = {
    {"swd_reset", 0, swd_reset},
    {"jtag_to_swd", 0, jtag_to_swd},
    {"write_select", 3, write_select_reg},
    {"read_dpid", 0, read_dpid},
    {"read_idrcode", 0, read_idrcode},
    {"write_abort", 1, write_abort},
    {"read_ctrlstat", 0, read_ctrlstat},
    {"clear_stickyerr", 0, clear_stickyerr},
    {"read_prot_status", 0, read_protect_status},
    {"read_erase_status", 0, read_erase_status},
    {"do_erase_all", 0, do_erase_all},
    {"control_debug_power", 1, control_debug_power},
    {"read_ap_addr", 1, read_ap_addr},
    {"read_ap_csw", 0, read_csw},
    {"read_tar", 0, read_tar},
    {"read_drw", 0, read_drw},
    {"write_tar", 1, write_tar},
    {"write_drw", 1, write_drw},
    {NULL, 0, NULL} // Must be last
};

SPIRegisters init_spi_or_die() {
    uint32_t* mem = create_gpio_mmap();
    if(!mem) {
        printf("Could not create RBPI GPIO memory map\n");
       exit(0);
    }
    SPIRegisters _spi_registers = init_aux_spi(mem);
    const uint32_t ENABLE_AUX_SPI1 = 0x2; // Bit 1
    *(_spi_registers.enable) = ENABLE_AUX_SPI1;

    // Now  adjust the control reg
    ControlReg control_reg = {
        .speed = 0x28,
        .chip_select_pattern = 0,
        .post_input_mode = 0,
        .variable_cs = 0,
        .variable_width = 1,
        .dout_hold_time = 4,
        .enable = 1,
        .in_rising = 1,
        .clear_fifos = 0,
        .out_rising = 1,
        .invert_clk =0,
        .msb_out_first = 0,
        .shift_length = 0,
        .cs_high_time = 0,
        .tx_empty_irq = 0,
        .done_irq = 0,
        .msb_in_first = 0,
        .keep_input = 0
        };
    write_control_reg(_spi_registers, control_reg);
    return _spi_registers;
}

void handle_line(char* line) {
    int i;
    const int MAX_ARGS=16;
    char* tokens[MAX_ARGS+1];
    uint32_t args[MAX_ARGS];
    int ntoks = 0;
    // first check if the first char is a '#' or the line is empty
    // if it is, treat this as a comment
    if(strlen(line) == 0 || line[0] == '#') {
        return ;
    }
    // Need to get the command name should just read until first space
    // Btw i'm only like 80% sure strtok return values are NULL terminated
    tokens[ntoks++] = strtok(line, " \t\n");

    if(tokens[0] == NULL) {
        // TODO add error massage
        return;
    }

    // Get the arguments
    while((tokens[ntoks] = strtok(NULL, " \n\t")) && ntoks++){ }

    // now compare the input string to all the commands in the command table to find
    // the matching name
    Command* thisCommand = &(commandTable[0]);
    while(1) {
        if(!thisCommand->name) {
            printf("%s is not a valid command\n", tokens[0]);
            return;
        }
        if(strcmp(thisCommand->name, tokens[0]) == 0) {
            break;
        }
        thisCommand++;
    }

    if(thisCommand->nargs != ntoks-1) {
        printf("Incorrect number of arguements supplied for \"%s\", %i given, %i required\n",
               thisCommand->name, thisCommand->nargs, ntoks-1);
        return;
    }

    for(i=0; i <ntoks-1; i++) {
        args[i] = strtoul(tokens[i+1], NULL, 0);
    }
    thisCommand->func(args);
}

void completion(const char *buf, linenoiseCompletions *lc) {
    // does buf have a null terminator? lets assume so lol
    int i = 0;
    Command* command = &(commandTable[0]);
    while(command->name) {
        if(strstr(command->name, buf) != 0) {
            linenoiseAddCompletion(lc, command->name);
        }
        command = &(commandTable[++i]);
    }
}

char* hint_string = NULL;
char *hints(const char *buf, int *color, int *bold) {
    return NULL;
    static const int BUF_LEN = 256;
    const char* arg_str = "arg";
    int i = 0;
    int j;
    unsigned int chars_added = 0;
    Command* command = &(commandTable[0]);
    while(command->name) {
        if (!strcasecmp(buf,command->name)) {
            hint_string = (char*) malloc(sizeof(char)*BUF_LEN);
            *color = 35;
            *bold = 0;
            for(j=1; j < command->nargs; j++) {
                snprintf(hint_string+chars_added, BUF_LEN-chars_added, " %s%i", arg_str, j);
                chars_added += strlen(arg_str)+2;
            }
            return hint_string;
        }
        command = &(commandTable[++i]);
    }
    return NULL;
}

int main(int argc, char **argv) {
    char *line;
    char *prgname = argv[0];

    /* Parse options, with --multiline we enable multi line editing. */
    while(argc > 1) {
        argc--;
        argv++;
        if (!strcmp(*argv,"--multiline")) {
            linenoiseSetMultiLine(1);
            printf("Multi-line mode enabled.\n");
        } else if (!strcmp(*argv,"--keycodes")) {
            linenoisePrintKeyCodes();
            exit(0);
        } else {
            fprintf(stderr, "Usage: %s [--multiline] [--keycodes]\n", prgname);
            exit(1);
        }
    }

    spi_registers = init_spi_or_die();

    /* Set the completion callback. This will be called every time the
     * user uses the <tab> key. */
    linenoiseSetCompletionCallback(completion);
    linenoiseSetHintsCallback(hints);

    /* Load history from file. The history file is just a plain text file
     * where entries are separated by newlines. */
    linenoiseHistoryLoad("history.txt"); /* Load the history at startup */

    /* Now this is the main loop of the typical linenoise-based application.
     * The call to linenoise() will block as long as the user types something
     * and presses enter.
     *
     * The typed string is returned as a malloc() allocated string by
     * linenoise, so the user needs to free() it. */
    
    while((line = linenoise("swd> ")) != NULL) {
        /* Do something with the string. */
        if (line[0] != '\0' && line[0] != '/') {
            handle_line(line);
            linenoiseHistoryAdd(line); /* Add to the history. */
            linenoiseHistorySave("history.txt"); /* Save the history on disk. */
        } else if (!strncmp(line,"/historylen",11)) {
            /* The "/historylen" command will change the history len. */
            int len = atoi(line+11);
            linenoiseHistorySetMaxLen(len);
        } else if (!strncmp(line, "/mask", 5)) {
            linenoiseMaskModeEnable();
        } else if (!strncmp(line, "/unmask", 7)) {
            linenoiseMaskModeDisable();
        } else if (line[0] == '/') {
            printf("Unreconized command: %s\n", line);
        }
        free(line);
    }
    clean_up_mmap();
    return 0;
}
