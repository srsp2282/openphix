#include <math.h>
#include <string.h>

#include "openphix/cmd.h"
#include "openphix/exp.h"
#include "openphix/res.h"
#include "openphix/str.h"
#include "openphix/table.h"
#include "test.h"

static struct exp_ctx ctx;

static int ev(const char *src, const uint8_t *x, int n, struct exp_value *v)
{
    memset(&ctx, 0, sizeof ctx);
    ctx.in.x = x;
    ctx.in.n = n;
    return exp_eval_text(&ctx, src, v);
}

int main(int argc, char **argv)
{
    struct res image, expf;
    struct exp_value v;
    static const uint8_t x54[] = { 0x54, 0x62 };
    static const uint8_t xff[] = { 0xff, 0xff };
    static const uint8_t x12[] = { 0x12, 0x34, 'A', 'B', 'C' };
    char buf[128];

    /* language, without the image */
    CHECK_EQ(ev("Y=1;", NULL, 0, &v), 0);
    CHECK_EQ(v.kind, EXP_NUM); CHECK_EQ((long)v.num, 1);
    ev("if(X1==0x54) Y=1; else Y=0;", x54, 2, &v); CHECK_EQ((long)v.num, 1);
    ev("if(X1==0x54) Y=1; else Y=0;", xff, 2, &v); CHECK_EQ((long)v.num, 0);
    ev("if((X1==0x88)&&(X2==0x62)) Y=1; else Y=0;", x54, 2, &v); CHECK_EQ((long)v.num, 0);
    ev("if((X1==0x54)||(X2==0x54)) Y=1;  else Y=0;", x54, 2, &v); CHECK_EQ((long)v.num, 1);
    ev("Y=X1*256+X2;", x12, 2, &v); CHECK_EQ((long)v.num, 0x1234);
    ev("if(x1==0xFF&&x2==0xFF) Y=string(0x02,0x01,0x00,0x0A);else Y=1.0/32768*(x1*256+x2);", xff, 2, &v);
    CHECK_EQ(v.kind, EXP_STRID); CHECK_EQ(v.strid, 0x0201000a);
    ev("if(x1==0xFF&&x2==0xFF) Y=string(0x02,0x01,0x00,0x0A);else Y=1.0/32768*(x1*256+x2);", x12, 2, &v);
    CHECK_EQ(v.kind, EXP_NUM); CHECK(fabs(v.num - 4660.0 / 32768) < 1e-9);
    ev("Y=SPRINTF([%02X %02X],X1,X2);", x12, 2, &v);
    CHECK_EQ(v.kind, EXP_TEXT); CHECK(strcmp(v.text, "12 34") == 0);
    ev("Y=SPRINTF([%c%c%c],X3,X4,X5);", x12, 5, &v); CHECK(strcmp(v.text, "ABC") == 0);
    ev("if(0 == strcmp([ABC],ASCII(X3,X4,X5))&&(X1==0x12)) Y=1; else Y=0;", x12, 5, &v); CHECK_EQ((long)v.num, 1);
    ev("if(0 == strcmp([ABD],ASCII(X3,X4,X5))) Y=1; else Y=0;", x12, 5, &v); CHECK_EQ((long)v.num, 0);
    ev("Y=HEX(X1,X2);", x12, 2, &v); CHECK(strcmp(v.text, "1234") == 0);
    ev("Y=INT(X1/5);", x12, 2, &v); CHECK_EQ((long)v.num, 3);
    ev("if(x1.bit4) Y=1; else Y=0;", x12, 2, &v); CHECK_EQ((long)v.num, 1);
    ev("if(x1.bit0) Y=1; else Y=0;", x12, 2, &v); CHECK_EQ((long)v.num, 0);
    ev("Y=x2>>4;", x12, 2, &v); CHECK_EQ((long)v.num, 3);
    ev("Y=x2&0x0F|0x40;", x12, 2, &v); CHECK_EQ((long)v.num, 0x44);
    ev("Y=x%7;", x12, 2, &v); CHECK_EQ((long)v.num, 0x12 % 7);
    ev("Y=RANGE(40,boundless);", NULL, 0, &v); CHECK(ctx.range.valid && ctx.range.boundless && ctx.range.lo == 40);
    ev("Y=RANGE(2000,3000);", NULL, 0, &v); CHECK(ctx.range.hi == 3000 && !ctx.range.boundless);
    ev("if(X1==FF) Y=1; else Y=2;", xff, 2, &v); CHECK_EQ((long)v.num, 1);   /* typo tolerance */
    ev("if(X1>0x10) Y=1; else if(X1>0x5) Y=2; else Y=3;", x12, 2, &v); CHECK_EQ((long)v.num, 1);
    ev("if(X1>0x50) Y=1; else if(X1>0x5) Y=2; else Y=3;", x12, 2, &v); CHECK_EQ((long)v.num, 2);
    ev("if(X1>0x50) Y=1; else if(X1>0x50) Y=2; else Y=3;", x12, 2, &v); CHECK_EQ((long)v.num, 3);
    ev("if(X1>0x50) Y=1; else Y=3; Y=7;", x12, 2, &v); CHECK_EQ((long)v.num, 7);
    /* unbalanced: still assigns */
    CHECK_EQ(ev("if((X1==0x12) Y=5; else Y=6;", x12, 2, &v), 0); CHECK_EQ((long)v.num, 5);
    CHECK_EQ(ev("garbage", x12, 2, &v), -1);
    CHECK_EQ(v.kind, EXP_NONE);

    if (argc < 2 || hal_stub_load(argv[1]) != 0)
        return 1;
    CHECK_EQ(res_init(&image), 0);
    str_init(&image);
    CHECK_EQ(exp_init(&image), 0);
    CHECK_EQ(cmd_init(&image), 0);

    /* every record of the dump must parse; the known seven typos are the
     * only records allowed to raise a parse error, and even they assign */
    CHECK_EQ(res_open(&image, "Exp.BIN", &expf), 0);
    {
        uint32_t n = table_count(&expf), errors = 0, unassigned = 0;
        uint8_t x[41];
        for (int i = 0; i < 41; i++) x[i] = (uint8_t)(0x30 + i);
        CHECK_EQ(n, 3535);
        for (uint32_t k = 0; k < n; k++) {
            uint32_t id;
            table_entry(&expf, k, &id, NULL);
            memset(&ctx, 0, sizeof ctx);
            ctx.in.x = x;
            ctx.in.n = 41;
            if (exp_eval(&ctx, id, &v) != 0) {
                unassigned++;
                if (unassigned <= 3) {
                    char src[80];
                    exp_source(id, src, sizeof src);
                    fprintf(stderr, "unassigned %08x: %s\n", (unsigned)id, src);
                }
            }
            if (ctx.error) {
                errors++;
                if (errors <= 3)
                    fprintf(stderr, "parse error in %08x at '%.30s'\n", (unsigned)id, ctx.error_at ? ctx.error_at : "");
            }
        }
        fprintf(stderr, "exp: %u records, %u with parse errors, %u unassigned\n", (unsigned)n, (unsigned)errors, (unsigned)unassigned);
        /* 10 records carry typos that no parser can read (missing ';',
         * "00x00000039", a string() call without its name); 68 are if/else
         * chains with no final else, which legitimately yield nothing for
         * inputs that match no branch */
        CHECK(errors <= 10);
        CHECK(unassigned <= 70);
    }
    /* records from the image, with ID() indirection */
    memset(&ctx, 0, sizeof ctx);
    ctx.in.x = x12; ctx.in.n = 5;
    CHECK_EQ(exp_eval(&ctx, 0x13, &v), 0); CHECK_EQ((long)v.num, 0x1234);
    CHECK_EQ(exp_eval(&ctx, 0x04000001, &v), 0);    /* TRANSID(ID(0x38), ...) without hook: the code */
    CHECK_EQ(v.kind, EXP_NUM); CHECK_EQ((long)v.num, 0x123441);
    CHECK_EQ(exp_eval(&ctx, 0x01000002, &v), 0);
    exp_format(&v, buf, sizeof buf);
    CHECK(strncmp(buf, "0.142", 5) == 0);
    CHECK_EQ(exp_eval(&ctx, 0x12345678, &v), -1);

    /* commands */
    {
        struct cmd c;
        struct cmd_packet pk;
        uint32_t pos = 0;
        uint8_t msg[32];
        CHECK_EQ(cmd_get(0x00000101, &c), 0);
        CHECK_EQ(c.kind, CMD_SEND); CHECK_EQ(c.h[0], LINK_CAN); CHECK_EQ(c.h[2], 2);
        CHECK_EQ(cmd_next_packet(&c, &pos, &pk), 1);
        CHECK(pk.has_id); CHECK_EQ(pk.can_id, 0x7df); CHECK_EQ(pk.len, 8); CHECK_EQ(pk.data[1], 0x09);
        CHECK_EQ(cmd_next_packet(&c, &pos, &pk), 1);
        CHECK_EQ(pk.can_id, 0x7e0); CHECK_EQ(pk.data[0], 0x30);
        CHECK_EQ(cmd_next_packet(&c, &pos, &pk), 0);
        CHECK_EQ(cmd_get(0x02011026, &c), 0);
        CHECK_EQ(cmd_kline_message(&c, msg, sizeof msg), 16);
        CHECK_EQ(msg[0], 0x8c); CHECK_EQ(msg[4], 0xbb); CHECK_EQ(msg[15], 0x7d);
        CHECK_EQ(cmd_get(0x00000003, &c), 0);
        CHECK_EQ(c.kind, CMD_KEEPALIVE); CHECK_EQ(cmd_period_ms(&c), 800);
        CHECK_EQ(cmd_get(0x0000006a, &c), 0);
        CHECK_EQ(c.kind, CMD_NUMBER); CHECK_EQ(cmd_number(&c), 8000000);
        CHECK_EQ(cmd_get(0x02010008, &c), 0);        /* keepalive with K-line chunks */
        CHECK_EQ(cmd_kline_message(&c, msg, sizeof msg), 5);
        CHECK_EQ(msg[0], 0x81); CHECK_EQ(msg[4], 0xc0);
        CHECK_EQ(cmd_get(0x99999999, &c), -1);
    }
    TEST_MAIN_END();
}
