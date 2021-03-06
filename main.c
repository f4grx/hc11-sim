/* working hc11 simulator for sys11 and others */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/time.h>

#include "log.h"
#include "core.h"
#include "sci.h"
#include "gdbremote.h"

static struct option long_options[] =
  {
    {"version"    , no_argument      , 0, 'v' },
    {"debug"      , no_argument      , 0, 'd' },
    {"no-gdb"     , no_argument      , 0, 'g' },
    {"bin"        , required_argument, 0, 'b' },
    {"s19"        , required_argument, 0, 's' },
    {"writable"   , no_argument      , 0, 'w' },
    {"preset-regs", required_argument, 0, 'p' },
    {"preset-mem" , required_argument, 0, 'm' },
    {"run"        , no_argument      , 0, 'r' },
    {"expect-regs", required_argument, 0, 'e' },

    {0         , 0                , 0,  0  }
  };

sem_t end;

struct hc11_regs initial_regs;
static uint8_t* loadbin(const char *fname, uint16_t *size)
  {
    FILE *f;
    uint32_t lsize;
    uint8_t *blob;

    f = fopen(fname, "rb");

    if(!f)
      {
        printf("Unable to open: %s\n",fname);
        return NULL;
      }

    fseek(f, 0, SEEK_END);
    lsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if(lsize > 65536)
      {
        printf("file %s is too big\n", fname);
err:
        fclose(f);
        return NULL;
      }
    blob = malloc(lsize);
    if(!blob)
      {
        printf("cannot alloc mem for %s\n", fname);
        goto err;
      }
    if(fread(blob, 1, lsize, f) != lsize)
      {
        printf("cannot read: %s\n", fname);
        free(blob);
        goto err;
      }

    fclose(f);
    *size = (uint16_t)lsize;
    return blob;
  }

void help(void)
  {
    printf("sim -d [-s,--s19 <file>] [-b,--bin <adr,file>] [-w,--writable]\n"
           "\n"
           "  -v --version              Display version info\n"
           "  -d --debug                Display verbose debug\n"
           "  -g --no-gdb               Do not start a GDB server\n"
           "  -s --s19 <file>           Load S-record file (not implemented yet)\n"
           "  -b --bin <adr,file>       Load binary file at address\n"
           "  -w --writable             Map 8K of RAM in monitor address space\n"
           "  -p --preset-regs          Set register values, comma-separated list\n"
           "                            each entry reg=[0x]val, reg in d,a,b,x,y,p,s,c\n"
           "  -m --preset-mem <adr,hex> load hex bytes at specified address\n"
           "  -r --run                  start executing instructions as soon as inits are done\n"
           "  -e --expect-regs          Set expected register values after execution\n"
         );
  }

void version(void)
  {
    printf("sys11 simulator v0.1 by f4grx (c) 2019-2020\n");
  }

void sig(int sig)
  {
    //printf("Signal caught\n");
    sem_post(&end);
  }

struct hc11_core core;

uint64_t getmicros(void)
  {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec * 1000000LU + tv.tv_usec;
  }

static int parse_preset_reg(struct hc11_core *core, char *param)
  {
    char *ptr = strchr(param,'=');
    int val;
    if(!ptr)
      {
        fprintf(stderr, "expected: , - reg=val, where reg = d,a,b,x,y,p(pc),s(sp),c(ccr)\n");
        return -1;
      }
    *ptr=0;
    ptr += 1;
    if(!*ptr)
      {
        fprintf(stderr, "expected: value - reg=val, where reg = d,a,b,x,y,p(pc),s(sp),c(ccr)\n");
      }
    val = strtol(ptr,NULL,0);
    //printf("reg %c value %d (%04X)\n",*param,val,val);
    switch(*param)
      {
        case 'd': core->regs.d   = val; break;
        case 'x': core->regs.x   = val; break;
        case 'y': core->regs.y   = val; break;
        case 'p': core->regs.pc  = val; break;
        case 's': core->regs.sp  = val; break;
        case 'c': core->regs.ccr = val; break;
        case 'a': core->regs.d   = (core->regs.d & 0x00FF) | ((val & 0xFF) << 8); break;
        case 'b': core->regs.d   = (core->regs.d & 0xFF00) |  (val & 0xFF)      ; break;
        default:
          fprintf(stderr,"unknown reg: %s\n",param);
          return -1;
      }
    return 0;
  }

static int parse_preset_regs(struct hc11_core *core, char *param)
  {
    //split using commas
    char *ptr;
    int   ret;
    ptr = param;
    while(*param)
      {
        while(*param && *param!=',') param++;
        if(*param!=0)
          {
            *param = 0;
            param++;
          }
        ret = parse_preset_reg(core,ptr);
        if(ret != 0)
          {
            return ret;
          }
        ptr = param;
      }
    return 0;
  }

static int parse_preset_mem(struct hc11_core *core, char *param)
  {
    char *ptr;
    uint16_t adr;
    uint16_t size;
    int val;
    uint8_t *bin;
    ptr = strchr(optarg, ',');
    if(!ptr)
      {
        fprintf(stderr,"--preset-mem adr,hexdata\n");
        return -1;
      }
    *ptr = 0;
    ptr++;
    adr = (uint16_t)strtoul(optarg, NULL, 0);
    size = strlen(ptr);
    if(size & 1)
      {
        fprintf(stderr,"bad hex data\n");
        return -1;
      }

    while(size>1)
      {
        if(sscanf(ptr,"%02X",&val) != 1)
          {
            fprintf(stderr, "bad hex data: %s\n",ptr);
            return -1;
          }
        hc11_core_writeb(core,adr,val&0xFF);
        adr  += 1;
        ptr  += 2;
        size -= 2;
      }

    return 0;
  }

static int parse_check_reg(struct hc11_core *core, char *param)
  {
    char *ptr = strchr(param,'=');
    int val;
    bool check;
    bool byte;
    uint16_t real;

    if(!ptr)
      {
        fprintf(stderr, "expected: , - reg=val, where reg = d,a,b,x,y,p(pc),s(sp),c(ccr)\n");
        return -1;
      }
    *ptr=0;
    ptr += 1;
    if(!*ptr)
      {
        fprintf(stderr, "expected: value - reg=val, where reg = d,a,b,x,y,p(pc),s(sp),c(ccr)\n");
      }
    val = strtol(ptr,NULL,0);
    byte = false;
    switch(*param)
      {
        case 'd': real = core->regs.d; break;
        case 'x': real = core->regs.x; break;
        case 'y': real = core->regs.y; break;
        case 'p': real = core->regs.pc; break;
        case 's': real = core->regs.sp; break;
        case 'a': byte = true; real = core->regs.d >> 8; break;
        case 'b': byte = true; real = core->regs.d & 0xFF; break;
        case 'c': byte = true; real = core->regs.ccr; break;
        default:
          fprintf(stderr,"unknown reg: %s\n",param);
          return -1;
      }
    if(byte)
      {
        val &= 0xFF;
        check = (real == val);
        if(!check)
          {
            printf("WARNING REG %s VALUE 0x%02X (%d) EXPECTED 0x%02X (%d)\n",param, real, real, val, val);
          }
      }
    else
      {
        val &= 0xFFFF;
        check = (real == val);
        if(!check)
          {
            printf("WARNING REG %s VALUE 0x%04X (%d) EXPECTED 0x%04X (%d)\n",param, real, real, val, val);
          }
      }
  }

int parse_check_regs(struct hc11_core *core, char *param)
  {
    //split using commas
    char *ptr;
    int   ret;
    ptr = param;
    while(*param)
      {
        while(*param && *param!=',') param++;
        if(*param!=0)
          {
            *param = 0;
            param++;
          }
        ret = parse_check_reg(core,ptr);
        if(ret != 0)
          {
          }
        ptr = param;
      }
    return 0;
  }

static void show_regs(struct hc11_core *core)
  {
    printf("PC=%04X D=%04X X=%04X Y=%04X SP=%04X CCR=%c%c%c%c%c%c%c%c\n",
            core->regs.pc,
            core->regs.d,
            core->regs.x,
            core->regs.y,
            core->regs.sp,
            core->regs.flags.S?'S':'-',
            core->regs.flags.X?'X':'-',
            core->regs.flags.I?'I':'-',
            core->regs.flags.H?'H':'-',
            core->regs.flags.N?'N':'-',
            core->regs.flags.Z?'Z':'-',
            core->regs.flags.V?'V':'-',
            core->regs.flags.C?'C':'-'
            );
  }

int main(int argc, char **argv)
  {
    struct hc11_sci *sci;
    struct gdbremote_t remote;
    int c;
    struct sigaction sa_mine;
    int val;
    int prev;
    uint64_t cycles;
    uint64_t micros;
    bool debug = false;
    bool dogdb = true;
    char *regcheck = NULL;

    log_init();

    sem_init(&end,0,0);
    memset(&sa_mine, 0, sizeof(struct sigaction));
    sa_mine.sa_handler = sig;
    sigaction(SIGINT, &sa_mine, NULL);

    hc11_core_init(&core);
    hc11_core_reset(&core);

    //map 32k of RAM in the first half of the address space
    hc11_core_map_ram(&core, "ram", 0x0000, 0x8000); //100h bytes masked by internal mem
    while (1)
      {
        int option_index = 0;
        c = getopt_long(argc, argv, "b:s:wdp:m:re:gv", long_options, &option_index);
        if (c == -1)
          {
            break;
          }
        switch (c)
          {
            case 'd':
              log_enable(0,0);
              debug = true;
              break;

            case 'g': dogdb = false; break;

            case 'b':
              {
                char *ptr;
                uint16_t adr;
                uint16_t size;
                uint8_t *bin;
                ptr = strchr(optarg, ',');
                if(!ptr)
                  {
                    printf("--mapbin adr,binfile\n");
                    return -1;
                  }
                *ptr = 0;
                ptr++;
                printf("map something: file %s @ %s\n", ptr, optarg);
                bin = loadbin(ptr,&size);
                if(!bin)
                  {
                    printf("map failed\n");
                    return -1;
                  }
                adr = (uint16_t)strtoul(optarg, NULL, 0);
                hc11_core_map_rom(&core, "rom", adr, size, bin);
                break;
              }

            case 'w':
              {
                hc11_core_map_ram(&core, "rom", 0xE000, 0x2000);
                break;
              }
            case 's':
              {
                printf("TODO load S19 file\n");
                return -1;
              }
            case 'p': //--preset-regs
              {
                if(parse_preset_regs(&core,optarg) != 0)
                  {
                    fprintf(stderr,"invalid preset regs\n");
                    return -1;
                  }
                break;
              }
            case 'm': //--preset-mem
              {
                if(parse_preset_mem(&core,optarg) != 0)
                  {
                    fprintf(stderr,"invalid preset mem\n");
                    return -1;
                  }
                break;
              }
            case 'r': //--run
              {
                core.status = STATUS_RUNNING;
                core.state  = 2; //set core to the FETCH state, avoiding any vector fetch
                break;
              }
            case 'e': //--expect-regs
              {
                regcheck = optarg;
                break;
              }
            case '?':
              {
                help();
                return -1;
                break;
              }

            default:
              printf("?? getopt returned character code 0%o ??\n", c);
          }
      }


    if (optind < argc)
      {
        printf("non-option ARGV-elements: ");
        while (optind < argc)
          {
            printf("%s ", argv[optind++]);
          }
        printf("\n");
      }

    sci = hc11_sci_init(&core);

    if(dogdb)
      {
        remote.port = 3333;
        remote.core = &core;
        gdbremote_init(&remote);
      }

    prev = -1;
    cycles = 0;
    micros = getmicros();
    while(1)
      {
        uint64_t newmicros = getmicros();
        uint64_t deltatime = newmicros - micros;
        if(deltatime > 1000000)
          {
            uint64_t deltacycles = core.clocks - cycles;
            cycles = core.clocks;
            micros = newmicros;
            if((core.status == STATUS_RUNNING) && !debug)
              {
                printf("Running at %.3f MHz     \r", (double)deltacycles/(double)deltatime);
              }
          }
        sem_getvalue(&end, &val);
        if(val)
          {
            printf("simulation interrupted\n");
            break;
          }

        if(prev != core.status)
          {
            if(debug) printf("status: %d -> %d\n", prev, core.status);
            if(core.status == STATUS_STOPPED && prev != -1)
              {
                if(dogdb)
                  {
                    gdbremote_stopped(&remote, (core.busadr == VECTOR_ILLEGAL) ?
                                               GDBREMOTE_STOP_FAIL :
                                               GDBREMOTE_STOP_NORMAL);
                  }
              }
            prev = core.status;
          }

        if(core.status == STATUS_STEPPING)
          {
            printf("doing a step\n");
            hc11_core_step(&core);
            if(debug) show_regs(&core);
            core.status = STATUS_STOPPED;
          }
        else if(core.status == STATUS_RUNNING)
          {
            hc11_core_step(&core);
            if(debug) show_regs(&core);
          }
        else if(core.status == STATUS_STOPPED)
          {
              usleep(10000);
          }
        else if(core.status == STATUS_EXECUTED_STOP)
          {
            //printf("Simulation ended\n");
            sem_post(&end);
            break;
          }
      }

    //If register check was selected, parse and compare regs
    if(regcheck)
      {
        parse_check_regs(&core, regcheck);
      }

    sem_getvalue(&end, &val);
    if(!val)
      {
        printf("Waiting for ctrl-c...\n");
        sem_wait(&end);
      }
    if(dogdb)
      {
        gdbremote_close(&remote);
      }
    hc11_sci_close(sci);
    if(debug)
      {
        hc11_core_istats(stdout, &core);
      }
  }

