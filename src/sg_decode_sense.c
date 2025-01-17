/*
 * Copyright (c) 2010-2023 Douglas Gilbert.
 * All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the BSD_LICENSE file.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <getopt.h>
#define __STDC_FORMAT_MACROS 1
#include <inttypes.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "sg_lib.h"
#include "sg_pr2serr.h"
#include "sg_json_sg_lib.h"
#include "sg_unaligned.h"


static const char * version_str = "1.43 20230517";

#define MY_NAME "sg_decode_sense"

#define MAX_SENSE_LEN 8192 /* max descriptor format actually: 255+8 */

static struct option long_options[] = {
    {"binary", required_argument, 0, 'b'},
    {"cdb", no_argument, 0, 'c'},
    {"err", required_argument, 0, 'e'},
    {"exit-status", required_argument, 0, 'e'},
    {"exit_status", required_argument, 0, 'e'},
    {"file", required_argument, 0, 'f'},
    {"help", no_argument, 0, 'h'},
    {"hex", no_argument, 0, 'H'},
    {"in", required_argument, 0, 'i'},          /* don't advertise */
    {"inhex", required_argument, 0, 'i'},       /* same as --file */
    {"ignore-first", no_argument, 0, 'I'},
    {"ignore_first", no_argument, 0, 'I'},
    {"json", optional_argument, 0, '^'},    /* short option is '-j' */
    {"js-file", required_argument, 0, 'J'},
    {"js_file", required_argument, 0, 'J'},
    {"list-err", no_argument, 0, 'l'},
    {"list_err", no_argument, 0, 'l'},
    {"nodecode", no_argument, 0, 'N'},
    {"nospace", no_argument, 0, 'n'},
    {"status", required_argument, 0, 's'},
    {"verbose", no_argument, 0, 'v'},
    {"version", no_argument, 0, 'V'},
    {"write", required_argument, 0, 'w'},
    {0, 0, 0, 0},
};

struct opts_t {
    bool do_binary;
    bool do_cdb;
    bool do_help;
    bool do_json;
    bool do_list_err;
    bool do_status;
    bool no_decode;
    bool no_space;
    bool verbose_given;
    bool version_given;
    bool err_given;
    bool file_given;
    bool ignore_first;
    const char * fname;
    int es_val;
    int es_up_val;
    int hex_count;
    int sense_len;
    int sstatus;
    int verbose;
    const char * wfname;
    const char * json_arg;
    const char * js_file;
    const char * no_space_str;
    sgj_state json_st;
    uint8_t sense[MAX_SENSE_LEN + 4];
};

static char concat_buff[1024];


static void
usage()
{
  pr2serr("Usage: sg_decode_sense [--binary=BFN] [--cdb] [--err=ES[,LES]] "
          "[--file=HFN]\n"
          "                       [--help] [--hex] [--inhex=HFN] "
          "[--ignore-first]\n"
          "                       [--json[=JO]] [--js_file=JFN] "
          "[--list-err]\n"
          "                       [--nodecode] [--nospace] [--status=SS] "
          "[--verbose]\n"
          "                       [--version] [--write=WFN] H1 H2 H3 ...\n"
          "  where:\n"
          "    --binary=BFN|-b BFN    BFN is a file name to read sense "
          "data in\n"
          "                          binary from. If BFN is '-' then read "
          "from stdin\n"
          "    --cdb|-c              decode given hex as cdb rather than "
          "sense data\n"
          "    --err=ES|-e ES        ES is Exit Status from utility in this "
          "package\n"
          "    --err=ES,LES|-e ES,LES    ES,LES is a range of exit status "
          "values\n"
          "    --file=HFN|-f HFN     HFN is a file name from which to read "
          "sense data\n"
          "                          in ASCII hexadecimal. Interpret '-' "
          "as stdin\n"
          "    --help|-h             print out usage message\n"
          "    --hex|-H              used together with --write=WFN, to "
          "write out\n"
          "                          C language style ASCII hex (instead "
          "of binary).\n"
          "                          Otherwise don't decode, output incoming "
          "data in\n"
          "                          hex (used '-HH' or '-HHH' for different "
          "formats)\n"
          "    --inhex=HFN|-i HFN    same as action as --file=HFN\n"
          "    --ignore-first|-I     when reading hex (e.g. with --file=HFN) "
          "skip\n"
          "                          the first hexadecimal value on each "
          "line\n"
          "    --json[=JO]|-j[=JO]    output in JSON instead of plain "
          "text\n"
          "                           Use --json=? for JSON help\n"
          "    --js-file=JFN|-J JFN    JFN is a filename to which JSON "
          "output is\n"
          "                            written (def: stdout); truncates "
          "then writes\n"
          "    --list-err|-l         list all error codes and meanings for "
          "sg3_utils\n"
          "    --nodecode|-N         do not decode, input hex or binary may "
          "be\n"
          "                          unrelated to SCSI sense or CDB formats\n"
          "    --nospace|-n          no spaces or other separators between "
          "pairs of\n"
          "                          hex digits (e.g. '3132330A')\n"
          "    --status=SS |-s SS    SCSI status value in hex\n"
          "    --verbose|-v          increase verbosity\n"
          "    --version|-V          print version string then exit\n"
          "    --write=WFN |-w WFN    write sense data in binary to WFN, "
          "create if\n"
          "                           required else truncate prior to "
          "writing\n\n"
          "Decodes SCSI sense data given on the command line as a sequence "
          "of\nhexadecimal bytes (H1 H2 H3 ...) . Alternatively the sense "
          "data can\nbe in a binary file or in a file containing ASCII "
          "hexadecimal. If\n'--cdb' is given then interpret hex as SCSI CDB "
          "rather than sense data.\nMay translate arbitrary hex data to "
          "binary and vice versa when\n--nodecode is given.\n"
          );
}

/* Handles short options after '-j' including a sequence of short options
 * that include one 'j' (for JSON). Want optional argument to '-j' to be
 * prefixed by '='. Return 0 for good, SG_LIB_SYNTAX_ERROR for syntax error
 * and SG_LIB_OK_FALSE for exit with no error. */
static int
chk_short_opts(const char sopt_ch, struct opts_t * op)
{
    /* only need to process short, non-argument options */
    switch (sopt_ch) {
    case 'c':
        op->do_cdb = true;
        break;
    case 'h':
    case '?':
        op->do_help = true;
        return 0;
    case 'H':
        op->hex_count++;
        break;
    case 'I':
        op->ignore_first = true;
        break;
    case 'j':
        break;  /* simply ignore second 'j' (e.g. '-jxj') */
    case 'l':
        op->do_list_err = true;
        break;
    case 'n':
        op->no_space = true;
        break;
    case 'N':
        op->no_decode = true;
        break;
    case 'v':
        op->verbose_given = true;
        ++op->verbose;
        break;
    case 'V':
        op->version_given = true;
        break;
    default:
        pr2serr("unrecognised option code %c [0x%x] ??\n", sopt_ch, sopt_ch);
        return SG_LIB_SYNTAX_ERROR;
    }
    return 0;
}

static int
parse_cmd_line(struct opts_t *op, int argc, char *argv[])
{
    int c, k, n, q;
    unsigned int ui;
    long val;
    char * avp;
    const char * ccp;
    char * endptr;

    while (1) {
        c = getopt_long(argc, argv, "^b:ce:f:hHi:Ij::J:lnNs:vVw:",
                        long_options, NULL);
        if (c == -1)
            break;

        switch (c) {
        case 'b':
            if (op->fname) {
                pr2serr("expect only one '--binary=BFN', '--file=HFN' or "
                        "'--inhex=HFN' option\n");
                return SG_LIB_CONTRADICT;
            }
            op->do_binary = true;
            op->fname = optarg;
            break;
        case 'c':
            op->do_cdb = true;
            break;
        case 'e':
            ccp = strchr(optarg, ',');
            n = sg_get_num_nomult(optarg);
            if ((n < 0) || (n > 255)) {
                pr2serr("--err= expected number from 0 to 255 inclusive\n");
                return SG_LIB_SYNTAX_ERROR;
            }
            op->err_given = true;
            op->es_val = n;
            if (ccp) {
                n = sg_get_num_nomult(ccp + 1);
                if ((n < 1) || (n > 255)) {
                    pr2serr("--err=<l>,<u> expected number from 1 to 255 "
                            "inclusive\n");
                    return SG_LIB_SYNTAX_ERROR;
                }
                op->es_up_val = n;
            }
            break;
        case 'f':
            if (op->fname) {
                pr2serr("expect only one '--binary=BFN', '--file=HFN' or "
                        "'--inhex=HFN' option\n");
                return SG_LIB_CONTRADICT;
            }
            op->file_given = true;
            op->fname = optarg;
            break;
        case 'h':
        case '?':
            op->do_help = true;
            return 0;
        case 'H':
            op->hex_count++;
            break;
        case 'i':
            if (op->fname) {
                pr2serr("expect only one '--binary=BFN', '--file=HFN' or "
                        "'--inhex=HFN' option\n");
                return SG_LIB_CONTRADICT;
            }
            op->file_given = true;
            op->fname = optarg;
            break;
        case 'I':
            op->ignore_first = true;
            break;
        case 'j':       /* for: -j[=JO] */
        case '^':       /* for: --json[=JO] */
            op->do_json = true;
            /* Now want '=' to precede all JSON optional arguments */
            if (optarg) {
                if ('^' == c) {
                    op->json_arg = optarg;
                    break;
                } else if ('=' == *optarg) {
                    op->json_arg = optarg + 1;
                    break;
                }
                n = strlen(optarg);
                for (k = 0; k < n; ++k) {
                    q = chk_short_opts(*(optarg + k), op);
                    if (SG_LIB_SYNTAX_ERROR == q)
                        return SG_LIB_SYNTAX_ERROR;
                    if (SG_LIB_OK_FALSE == q)
                        return 0;
                }
            } else
                op->json_arg = NULL;
            break;
       case 'J':
            op->do_json = true;
            op->js_file = optarg;
            break;
        case 'l':
            op->do_list_err = true;
            break;
        case 'n':
            op->no_space = true;
            break;
        case 'N':
            op->no_decode = true;
            break;
        case 's':
            if (1 != sscanf(optarg, "%x", &ui)) {
                pr2serr("'--status=SS' expects a byte value\n");
                return SG_LIB_SYNTAX_ERROR;
            }
            if (ui > 0xff) {
                pr2serr("'--status=SS' byte value exceeds FF\n");
                return SG_LIB_SYNTAX_ERROR;
            }
            op->do_status = true;
            op->sstatus = ui;
            break;
        case 'v':
            op->verbose_given = true;
            ++op->verbose;
            break;
        case 'V':
            op->version_given = true;
            break;
        case 'w':
            op->wfname = optarg;
            break;
        default:
            pr2serr("unrecognised option code %c [0x%x] ??\n", c, c);
            return SG_LIB_SYNTAX_ERROR;
        }
    }
    if (op->err_given)
        goto the_end;

    while (optind < argc) {
        avp = argv[optind++];
        if (op->no_space) {
            if (op->no_space_str) {
                if ('\0' == concat_buff[0]) {
                    if (strlen(op->no_space_str) > sizeof(concat_buff)) {
                        pr2serr("'--nospace' concat_buff overflow\n");
                        return SG_LIB_SYNTAX_ERROR;
                    }
                    strcpy(concat_buff, op->no_space_str);
                }
                if ((strlen(concat_buff) + strlen(avp)) >=
                    sizeof(concat_buff)) {
                    pr2serr("'--nospace' concat_buff overflow\n");
                    return SG_LIB_SYNTAX_ERROR;
                }
                if (op->version_given)
                    pr2serr("'--nospace' and found whitespace so "
                            "concatenate\n");
                strcat(concat_buff, avp);
                op->no_space_str = concat_buff;
            } else
                op->no_space_str = avp;
            continue;
        }
        val = strtol(avp, &endptr, 16);
        if (*avp == '\0' || *endptr != '\0' || val < 0x00 || val > 0xff) {
            pr2serr("Invalid byte '%s'\n", avp);
            return SG_LIB_SYNTAX_ERROR;
        }

        if (op->sense_len > MAX_SENSE_LEN) {
            pr2serr("sense data too long (max. %d bytes)\n", MAX_SENSE_LEN);
            return SG_LIB_SYNTAX_ERROR;
        }
        op->sense[op->sense_len++] = (uint8_t)val;
    }
the_end:
    return 0;
}

static void
enumerate_err_codes(const struct opts_t *op)
{
    int k = 0;
    int last = 127;
    char b[168];
    static const int blen = sizeof(b);

    if (op->err_given && (! op->do_list_err)) {
        if (! sg_exit2str(op->es_val, op->verbose > 1, blen, b))
            snprintf(b, blen, "Unable to decode exit status %d", op->es_val);
        if (1 & op->verbose) /* odd values of verbose print to stderr */
            pr2serr("%s\n", b);
        else    /* even values of verbose (including not given) to stdout */
            printf("%s\n", b);
        return;
    }
    if (op->err_given) {
        k = op->es_val;
        if (op->es_up_val > 0)
            last = op->es_up_val;
    }
    for ( ; k <= last; ++k) {
        if (sg_exit2str(k, op->verbose > 1, blen, b)) {
            if (1 & op->verbose) /* odd values of verbose print to stderr */
                pr2serr("%d: %s\n", k, b);
            else    /* even values of verbose (incl. not given) to stdout */
                printf("%d: %s\n", k, b);
        }
    }
}

/* Keep this format (e.g. 0xff,0x12,...) for backward compatibility */
static void
write2wfn(FILE * fp, struct opts_t * op)
{
    int k, n;
    size_t s;
    char b[128];

    for (k = 0, n = 0; k < op->sense_len; ++k) {
        n += sprintf(b + n, "0x%02x,", op->sense[k]);
        if (15 == (k % 16)) {
            b[n] = '\n';
            s = fwrite(b, 1, n + 1, fp);
            if ((int)s != (n + 1))
                pr2serr("only able to write %d of %d bytes to %s\n",
                        (int)s, n + 1, op->wfname);
            n = 0;
        }
    }
    if (n > 0) {
        b[n] = '\n';
        s = fwrite(b, 1, n + 1, fp);
        if ((int)s != (n + 1))
            pr2serr("only able to write %d of %d bytes to %s\n", (int)s,
                    n + 1, op->wfname);
    }
}


int
main(int argc, char *argv[])
{
    bool as_json = false;
    int k, err, blen;
    int ret = 0;
    unsigned int ui;
    size_t s;
    struct opts_t * op;
    FILE * fp = NULL;
    const char * cp;
    sgj_state * jsp;
    sgj_opaque_p jop = NULL;
    uint8_t * free_op_buff = NULL;
    char b[2048];

    if (getenv("SG3_UTILS_INVOCATION"))
        sg_rep_invocation(MY_NAME, version_str, argc, argv, stderr);
    op = (struct opts_t *)sg_memalign(sizeof(*op), 0 /* page align */,
                                      &free_op_buff, false);
    if (NULL == op) {
        pr2serr("Unable to allocate heap for options structure\n");
        ret = sg_convert_errno(ENOMEM);
        goto clean_op;
    }
    blen = sizeof(b);
    memset(b, 0, blen);
    ret = parse_cmd_line(op, argc, argv);

#ifdef DEBUG
    pr2serr("In DEBUG mode, ");
    if (op->verbose_given && op->version_given) {
        pr2serr("but override: '-vV' given, zero verbose and continue\n");
        op->verbose_given = false;
        op->version_given = false;
        op->verbose = 0;
    } else if (! op->verbose_given) {
        pr2serr("set '-vv'\n");
        op->verbose = 2;
    } else
        pr2serr("keep verbose=%d\n", op->verbose);
#else
    if (op->verbose_given && op->version_given)
        pr2serr("Not in DEBUG mode, so '-vV' has no special action\n");
#endif
    if (op->version_given) {
        pr2serr("version: %s\n", version_str);
        goto clean_op;
    }
    if (ret != 0) {
        usage();
        goto clean_op;
    } else if (op->do_help) {
        usage();
        goto clean_op;
    }
    if (op->do_list_err || op->err_given) {
        enumerate_err_codes(op);
        goto clean_op;
    }
    jsp = &op->json_st;
    if (op->do_json) {
       if (! sgj_init_state(jsp, op->json_arg)) {
            int bad_char = jsp->first_bad_char;
            char e[1500];

            if (bad_char) {
                pr2serr("bad argument to --json= option, unrecognized "
                        "character '%c'\n\n", bad_char);
            }
            sg_json_usage(0, e, sizeof(e));
            pr2serr("%s", e);
            ret = SG_LIB_SYNTAX_ERROR;
            goto fini;
        }
        jop = sgj_start_r(MY_NAME, version_str, argc, argv, jsp);
    }
    as_json = jsp->pr_as_json;


    if (op->do_status) {
        sg_get_scsi_status_str(op->sstatus, blen, b);
        printf("SCSI status: %s\n", b);
    }

    if ((0 == op->sense_len) && op->no_space_str) {
        if (op->verbose > 2)
            pr2serr("no_space str: %s\n", op->no_space_str);
        cp = op->no_space_str;
        for (k = 0; isxdigit((uint8_t)cp[k]) &&
                    isxdigit((uint8_t)cp[k + 1]); k += 2) {
            if (1 != sscanf(cp + k, "%2x", &ui)) {
                pr2serr("bad no_space hex string: %s\n", cp);
                ret = SG_LIB_SYNTAX_ERROR;
                goto fini;
            }
            op->sense[op->sense_len++] = (uint8_t)ui;
        }
    }

    if ((0 == op->sense_len) && (! op->do_binary) && (! op->file_given)) {
        if (op->do_status) {
            ret = 0;
            goto fini;
        }
        pr2serr(">> Need sense/cdb/arbitrary data on the command line or "
                "in a file\n\n");
        usage();
        ret = SG_LIB_SYNTAX_ERROR;
        goto fini;
    }
    if (op->sense_len && (op->do_binary || op->file_given)) {
        pr2serr(">> Need sense data on command line or in a file, not "
                "both\n\n");
        ret = SG_LIB_CONTRADICT;
        goto fini;
    }
    if (op->do_binary && op->file_given) {
        pr2serr(">> Either a binary file or a ASCII hexadecimal, file not "
                "both\n\n");
        ret = SG_LIB_CONTRADICT;
        goto fini;
    }

    if (op->do_binary) {
        fp = fopen(op->fname, "r");
        if (NULL == fp) {
            err = errno;
            pr2serr("unable to open file: %s: %s\n", op->fname,
                    safe_strerror(err));
            ret = sg_convert_errno(err);
            goto fini;
        }
        s = fread(op->sense, 1, MAX_SENSE_LEN, fp);
        fclose(fp);
        if (0 == s) {
            pr2serr("read nothing from file: %s\n", op->fname);
            ret = SG_LIB_SYNTAX_ERROR;
            goto fini;
        }
        op->sense_len = s;
    } else if (op->file_given) {
        ret = sg_f2hex_arr(op->fname, false, op->no_space, op->sense,
                           &op->sense_len,
                           (op->ignore_first ? -MAX_SENSE_LEN :
                                               MAX_SENSE_LEN));
        if (ret) {
            pr2serr("unable to decode ASCII hex from file: %s\n", op->fname);
            goto fini;
        }
        if (op->verbose > 1)
            pr2serr("%d bytes read successfully from %s\n", op->sense_len,
                    op->fname);
    }

    if (op->sense_len > 0) {
        if (op->wfname || op->hex_count) {
            if (op->wfname) {
                if (NULL == ((fp = fopen(op->wfname, "w")))) {
                    err =errno;
                    perror("open");
                    pr2serr("trying to write to %s\n", op->wfname);
                    ret = sg_convert_errno(err);
                    goto fini;
                }
            } else
                fp = stdout;

            if (op->wfname && (1 == op->hex_count))
                write2wfn(fp, op);
            else if (op->hex_count && (2 != op->hex_count))
                dStrHexFp((const char *)op->sense, op->sense_len,
                           ((1 == op->hex_count) ? 1 : -1), fp);
            else if (op->hex_count)
                dStrHexFp((const char *)op->sense, op->sense_len, 0, fp);
            else {
                s = fwrite(op->sense, 1, op->sense_len, fp);
                if ((int)s != op->sense_len)
                    pr2serr("only able to write %d of %d bytes to %s\n",
                            (int)s, op->sense_len, op->wfname);
            }
            if (op->wfname)
                fclose(fp);
        } else if (op->no_decode) {
            if (op->verbose > 1)
                pr2serr("Not decoding as %s because --nodecode given\n",
                        (op->do_cdb ? "cdb" : "sense"));
        } else if (op->do_cdb) {
            int sa, opcode;

            opcode = op->sense[0];
            if ((0x75 == opcode) || (0x7e == opcode) || (op->sense_len > 16))
                sa = sg_get_unaligned_be16(op->sense + 8);
            else if (op->sense_len > 1)
                sa = op->sense[1] & 0x1f;
            else
                sa = 0;
            sg_get_opcode_sa_name(opcode, sa, 0, blen, b);
            printf("%s\n", b);
        } else {
            if (as_json) {
                sgj_js_sense(jsp, jop, op->sense, op->sense_len);
                if (jsp->pr_out_hr) {
                    sg_get_sense_str(NULL, op->sense, op->sense_len,
                                     op->verbose, blen, b);
                     sgj_hr_str_out(jsp, b, strlen(b));
                }
            } else {
                sg_get_sense_str(NULL, op->sense, op->sense_len,
                                 op->verbose, blen, b);
                printf("%s\n", b);
            }
        }
    }
fini:
    ret = (ret >= 0) ? ret : SG_LIB_CAT_OTHER;
    if (as_json) {
        fp = stdout;

        if (op->js_file) {
            if ((1 != strlen(op->js_file)) || ('-' != op->js_file[0])) {
                fp = fopen(op->js_file, "w");   /* truncate if exists */
                if (NULL == fp) {
                    int e = errno;

                    pr2serr("unable to open file: %s [%s]\n", op->js_file,
                            safe_strerror(e));
                    ret = sg_convert_errno(e);
                }
            }
            /* '--js-file=-' will send JSON output to stdout */
        }
        if (fp)
            sgj_js2file(jsp, NULL, ret, fp);
        if (op->js_file && fp && (stdout != fp))
            fclose(fp);
        sgj_finish(jsp);
    }
clean_op:
    if (free_op_buff)
        free(free_op_buff);
    return ret;
}
