/*
* This program and the accompanying materials are made available under the terms of the *
* Eclipse Public License v2.0 which accompanies this distribution, and is available at *
* https://www.eclipse.org/legal/epl-v20.html                                      *
*                                                                                 *
* SPDX-License-Identifier: EPL-2.0                                                *
*                                                                                 *
* Copyright Contributors to the Zowe Project.                                     *
*/

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <gskcms.h>

#include "keyring_types.h"
#include "keyring_get.h"

#define CERTIFICATE_HEADER "-----BEGIN CERTIFICATE-----\n"
#define CERTIFICATE_FOOTER "-----END CERTIFICATE-----\n"
#define PRIVATE_HEADER "-----BEGIN PRIVATE KEY-----\n"
#define PRIVATE_FOOTER "-----END PRIVATE KEY-----\n"

int debug = 0;

int main(int argc, char **argv)
{
    int i;
    R_datalib_parm_list_64 p;
    Command_line_parms parms;

    if (getenv("KEYRING_UTIL_DEBUG") != NULL && ! strcmp(getenv("KEYRING_UTIL_DEBUG"), "YES")) {
        debug = 1;
    }
    memset(&parms, 0, sizeof(Command_line_parms));
    process_cmdline_parms(&parms, argc, argv);

    R_datalib_function function_table[] = {
        {"NEWRING", NEWRING_CODE, 0x00000000, 0, NULL, simple_action},
        {"DELCERT", DELCERT_CODE, 0x00000000, 0, NULL, delcert_action},
        {"DELRING", DELRING_CODE, 0x00000000, 0, NULL, simple_action},
        {"REFRESH", REFRESH_CODE, 0x00000000, 0, NULL, simple_action},
        {"GETCERT", GETCERT_CODE, 0x80000000, 0, NULL, getcert_action},
        {"HELP",    HELP_CODE,    0x00000000, 0, NULL, print_help},
        {"NOTSUPPORTED", NOTSUPPORTED_CODE, 0x00000000, 0, NULL, print_help}
    };

    R_datalib_function function;
    for (i = 0; i < sizeof(function_table)/sizeof(R_datalib_function); i++) {
        if (strncasecmp(function_table[i].name, parms.function, sizeof(parms.function)) == 0) {
            function = function_table[i];
            break;
        }
        function = function_table[sizeof(function_table)/sizeof(R_datalib_function) - 1];
    }
    if (debug) {
        printf("Selected function is %s with code of %.2X\n", function.name, function.code);
    }
    function.action(&p, &function, &parms);

    return 0;
}

void getcert_action(R_datalib_parm_list_64* rdatalib_parms, void * function, Command_line_parms* parms) {
    R_datalib_function *func = function;

    if (debug) {
        printf("%s action\n", func->name);
    }
    Data_get_buffers buffers;
    Return_codes ret_codes;
    memset(&ret_codes, 0, sizeof(Return_codes));
    memset(&buffers, 0, sizeof(Data_get_buffers));

    get_data(parms->userid, parms->keyring, parms->label, &buffers, &ret_codes);

    if (ret_codes.SAF_return_code != 0) {
        printf("R_datalib call failed: function code: %.2X, SAF rc: %d, RACF rc: %d, RACF rsn: %d\n",
            ret_codes.function_code, ret_codes.SAF_return_code, ret_codes.RACF_return_code, ret_codes.RACF_reason_code);
        return;
    }
    dump_certificate_and_key(&buffers);
}

void simple_action(R_datalib_parm_list_64* rdatalib_parms, void * function, Command_line_parms* parms) {
    R_datalib_function *func = function;
    if (debug) {
        printf("%s action\n", func->name);
    }
    set_up_R_datalib_parameters(rdatalib_parms, function, parms->userid, parms->keyring);
    invoke_R_datalib(rdatalib_parms);
    check_return_code(rdatalib_parms);
}

void delcert_action(R_datalib_parm_list_64* rdatalib_parms, void * function, Command_line_parms* parms) {
    R_datalib_function *func = function;

    R_datalib_data_remove rem_parm;
    memset(&rem_parm, 0x00, sizeof(R_datalib_data_remove));

    func->parmlist = &rem_parm;

    if (debug) {
        printf("%s action\n", func->name);
    }
    rem_parm.label_len = strlen(parms->label);
    rem_parm.label_addr = parms->label;
    rem_parm.CERT_userid_len = 0x00;

    set_up_R_datalib_parameters(rdatalib_parms, func, parms->userid, parms->keyring);
    invoke_R_datalib(rdatalib_parms);
    check_return_code(rdatalib_parms);
    // refresh DIGTCERT class if required
    if (rdatalib_parms->return_code == 4 && rdatalib_parms->RACF_return_code == 4 && rdatalib_parms->RACF_reason_code == 12) {
        printf("DIGTCERT class has to refreshed.\n");
        func->code = REFRESH_CODE;
        set_up_R_datalib_parameters(rdatalib_parms, func, "", "");
        invoke_R_datalib(rdatalib_parms);
        check_return_code(rdatalib_parms);
        printf("DIGTCERT class refreshed.\n");
    }
}

void dump_certificate_and_key(Data_get_buffers *buffers) {
    char filename[40];

    memset(filename, 0, strlen(filename));
    strcpy(filename, buffers->label);
    strcat(filename,".pem");

    write_to_file(filename, buffers->certificate, buffers->certificate_length, FALSE);

    memset(filename, 0, strlen(filename));
    strcpy(filename, buffers->label);
    strcat(filename,".key");

    // write_to_file(filename, buffers->private_key, buffers->private_key_length, TRUE);
}

void write_to_file(char *filename, char *ptr, int len, int isPrivate) {
    FILE *stream;
    int numwritten;
    gsk_buffer buf_in = {len, ptr};
    gsk_buffer buf_out = {0, 0};
    gsk_status rc;

    rc = gsk_encode_base64(&buf_in, &buf_out);
    if (debug) printf("gsk_encode_base64 rc=%d\n", rc);

    if ((stream = fopen(filename, "w")) == NULL) {
        printf("Could not open %s file.\n", filename);
        return;
    }

    isPrivate ? fprintf(stream, PRIVATE_HEADER) : fprintf(stream, CERTIFICATE_HEADER);
    numwritten = fwrite(buf_out.data, sizeof(char), buf_out.length, stream);
    isPrivate ? fprintf(stream, PRIVATE_FOOTER) : fprintf(stream, CERTIFICATE_FOOTER);

    gsk_free_buffer(&buf_out);

    if (debug)
        printf("Number of characters written to %s file is %d\n", filename, numwritten);

    if (fclose(stream)) {
        printf("fclose error.\n");
    }
}

void validate_and_set_parm(char * parm, char * cmd_parm, int maxlen) {
    if (strlen(cmd_parm) <= maxlen) {
        strcpy(parm, cmd_parm);
    } else {
        printf("ERROR: %s parm too long and will not be set.\n", cmd_parm);
    }
}

void check_return_code(R_datalib_parm_list_64* p) {
    if (p->return_code != 0 || p->RACF_return_code != 0 || p->RACF_reason_code != 0) {
        printf("Function code: %.2X, SAF rc: %d, RACF rc: %d, RACF rsn: %d\n",
            p->function_code, p->return_code, p->RACF_return_code, p->RACF_reason_code);
    }
}

void process_cmdline_parms(Command_line_parms* parms, int argc, char** argv) {
    int i;
    for (i = 1; i < argc; i++) {
        if (debug) {
            printf("%d. parameter: %s\n", i, argv[i]);
        }
        switch(i) {
            case 1:
                validate_and_set_parm(parms->function, argv[i], MAX_FUNCTION_LEN);
                break;
            case 2:
                validate_and_set_parm(parms->userid, argv[i], MAX_USERID_LEN);
                break;
            case 3:
                validate_and_set_parm(parms->keyring, argv[i], MAX_KEYRING_LEN);
                break;
            case 4:
                validate_and_set_parm(parms->label, argv[i], MAX_LABEL_LEN);
                break;
            default:
                printf("WARNING: %i. parameter - %s - is currently not supported and will be ignored.\n", i, argv[i]);
        }
    }
}



void print_help(R_datalib_parm_list_64* rdatalib_parms, void * function, Command_line_parms* parms) {
    printf("----------------------------------------------------\n");
    printf("Usage: keyring-util function userid keyring label\n");
    printf("----------------------------------------------------\n");
    printf("function:\n");
    printf("NEWRING - creates a new keyring.\n");
    printf("DELRING - deletes a keyring\n");
    printf("DELCERT - disconnects a certificate (label) from a keyring or deletes a certificate from RACF database\n");
    printf("REFRESH - refreshes DIGTCERT class\n");
    printf("HELP    - prints this help\n");
}
