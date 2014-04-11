/* Main simulator entry points specific to the EPIPHANY.
   Copyright (C) 1996, 1997, 1998, 1999, 2003, 2007, 2008, 2011
   Free Software Foundation, Inc.
   Contributed by Embecosm on behalf of Adapteva.

   This file is part of GDB, the GNU debugger.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include "sim-main.h"
#include "sim-options.h"

#if (WITH_HW)
#include "sim-hw.h"
#endif

#include "libiberty.h"
#include "bfd.h"

#ifdef HAVE_STRING_H
#include <string.h>
#else
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_CTYPE_H
#include <ctype.h>
#endif

#include "sim-options.h"

#if WITH_EMESH_SIM
#include "emesh.h"
#endif

static void free_state (SIM_DESC);
static void print_epiphany_misc_cpu (SIM_CPU *cpu, int verbose);

SIM_RC sim_esim_cpu_relocate (SIM_DESC sd, int extra_bytes);

/* Records simulator descriptor so utilities like epiphany_dump_regs can be
   called from gdb.  */
SIM_DESC current_state=0;
int is_sim_opened=0;

/* Cover function of sim_state_free to free the cpu buffers as well.  */

static void
free_state (SIM_DESC sd)
{
  if (STATE_MODULES (sd) != NULL)
    sim_module_uninstall (sd);
#if WITH_EMESH_SIM
  es_cleanup(STATE_ESIM(sd));
#else
  sim_cpu_free_all (sd);
#endif
  sim_state_free (sd);
}

static unsigned epiphany_add_ext_mem = 1;

static SIM_RC
epiphany_extenal_memory_option_handler (SIM_DESC sd, sim_cpu *cpu, int opt, char *arg, int is_command) {

	if(strcmp(arg,"off") == 0 ) {
		epiphany_add_ext_mem = 0;
	}
	if(strcmp(arg,"on") == 0 ) {
		epiphany_add_ext_mem = 1;
	}
	return SIM_RC_OK;
}
#if WITH_EMESH_SIM
static SIM_RC
epiphany_coreid_option_handler (SIM_DESC sd, sim_cpu *cpu, int opt, char *arg, int is_command) {
  char * endp;
  unsigned long ul = strtoul (arg, &endp, 0);
  unsigned old, new;

  if (! isdigit (arg[0]) || ul == 0 || ul >= 4096)
    {
      sim_io_eprintf (sd, "Invalid coreid `%s'. Valid range [1-4095]"
		      " ([0x001-0xFFF])\n", arg);
      return SIM_RC_FAIL;
    }
  new = (unsigned) ul;

  if (!es_valid_coreid(STATE_ESIM(sd), new))
    {
      sim_io_eprintf (sd, "coreid `%s' is not valid since it does not belong "
		      "to this node.\n", arg);
      return SIM_RC_FAIL;
    }

  if (STATE_ESIM(sd)->coreid == new)
    return SIM_RC_OK;

  if (!es_set_coreid(STATE_ESIM(sd), new))
    {
      sim_io_eprintf (sd, "Could not set coreid to `%s'. Maybe it was already "
		      "reserved by another sim process.\n", arg);
      return SIM_RC_FAIL;
    }

  if (sim_esim_cpu_relocate (sd, cgen_cpu_max_extra_bytes ()) != SIM_RC_OK)
    {
      return SIM_RC_FAIL;
    }
  return SIM_RC_OK;
}
#endif

static const OPTION options_epiphany[] =
{

  { {"epiphany-extenal-memory", optional_argument, NULL, 'e'},
      'e', "off|on", "Turn off/on the external memory region",
      epiphany_extenal_memory_option_handler  },
#if EMESH_SIM
  { {"coreid", required_argument, NULL, 0},
      '\0', "COREID", "Set coreid",
      epiphany_coreid_option_handler  },
#endif

  { {NULL, no_argument, NULL, 0}, '\0', NULL, NULL, NULL }
};



#if WITH_EMESH_SIM
/* Custom sim cpu alloc for emesh sim */
SIM_RC
sim_esim_cpu_relocate (SIM_DESC sd, int extra_bytes)
{
  static unsigned freed = 0;
  sim_cpu *new_cpu;
  if (! (new_cpu = es_set_cpu_state(STATE_ESIM(sd), STATE_CPU(sd, 0),
			sizeof(sim_cpu) + extra_bytes)))
    return SIM_RC_FAIL;

  if (!freed)
    {
      sim_cpu_free(STATE_CPU(sd, 0));
      freed = 1;
    }
  STATE_CPU(sd, 0) = new_cpu;
  return SIM_RC_OK;
}

SIM_RC sim_esim_init(SIM_DESC sd)
{
  /* TODO: Of course this shouldn't be hard coded */
  es_cluster_cfg cluster;
  es_node_cfg node;

  memset(STATE_ESIM(sd), 0, sizeof(es_state));
  memset(&node, 0, sizeof(node));
  memset(&cluster, 0, sizeof(cluster));

  node.rank = 0;
  cluster.nodes = 1;
  cluster.row_base = 32;
  cluster.col_base = 8;
  cluster.rows = 2;
  cluster.cols = 2;
  cluster.core_mem_region = 1024*1024;
  cluster.ext_ram_size = 32*1024*1024;
  cluster.ext_ram_node = 0;
  cluster.ext_ram_base = 0x8e000000;
  if (es_init(STATE_ESIM(sd), node, cluster))
    {
      return SIM_RC_FAIL;
    }

  return SIM_RC_OK;
}
#endif /* WITH_EMESH_SIM */

/* Create an instance of the simulator.  */

SIM_DESC
sim_open (kind, callback, abfd, argv)
     SIM_OPEN_KIND kind;
     host_callback *callback;
     struct bfd *abfd;
     char **argv;
{
  SIM_DESC sd = sim_state_alloc (kind, callback);
  char c;
  int i;

  /* The cpu data is kept in a separately allocated chunk of memory.  */
  if (sim_cpu_alloc_all (sd, 1, cgen_cpu_max_extra_bytes ()) != SIM_RC_OK)
    {
      free_state (sd);
      return 0;
    }

#if WITH_EMESH_SIM
  if (sim_esim_init(sd) != SIM_RC_OK)
    {
      sim_io_eprintf(sd, "Failed to initialize esim\n");
      free_state (sd);
      return 0;
    }
#endif


#if 0 /* FIXME: pc is in mach-specific struct */
  /* FIXME: watchpoints code shouldn't need this */
  {
    SIM_CPU *current_cpu = STATE_CPU (sd, 0);
    STATE_WATCHPOINTS (sd)->pc = &(PC);
    STATE_WATCHPOINTS (sd)->sizeof_pc = sizeof (PC);
  }
#endif

  if (sim_pre_argv_init (sd, argv[0]) != SIM_RC_OK)
    {
      free_state (sd);
      return 0;
    }

#ifdef HAVE_DV_SOCKSER /* FIXME: was done differently before */
  if (dv_sockser_install (sd) != SIM_RC_OK)
    {
      free_state (sd);
      return 0;
    }
#endif

#if 0 /* FIXME: 'twould be nice if we could do this */
  /* These options override any module options.
     Obviously ambiguity should be avoided, however the caller may wish to
     augment the meaning of an option.  */
  if (extra_options != NULL)
    sim_add_option_table (sd, extra_options);
#endif


  sim_add_option_table (sd, NULL, options_epiphany);

  /* getopt will print the error message so we just have to exit if this fails.
     FIXME: Hmmm...  in the case of gdb we need getopt to call
     print_filtered.  */
  if (sim_parse_args (sd, argv) != SIM_RC_OK)
    {
      free_state (sd);
      return 0;
    }

#if WITH_EMESH_SIM
  /* Coreid must be set on command-line when running in stand-alone */
  if (STATE_OPEN_KIND (sd) == SIM_OPEN_STANDALONE)
    {
      if (!es_valid_coreid(STATE_ESIM(sd), STATE_ESIM(sd)->coreid))
	{
	  sim_io_eprintf(sd, "Coreid must be set. Set with --coreid\n");
	  free_state (sd);
	  return 0;
	}
    }
#endif

#if 0
  /* Allocate a handler for the control registers and other devices
     if no memory for that range has been allocated by the user.
     All are allocated in one chunk to keep things from being
     unnecessarily complicated.  */
  if (sim_core_read_buffer (sd, NULL, read_map, &c, EPIPHANY_DEVICE_ADDR, 1) == 0)
    sim_core_attach (sd, NULL,
		     0 /*level*/,
		     access_read_write,
		     0 /*space ???*/,
		     EPIPHANY_DEVICE_ADDR, EPIPHANY_DEVICE_LEN /*nr_bytes*/,
		     0 /*modulo*/,
		     &epiphany_devices,
		     NULL /*buffer*/);

#endif

  /* Allocate core managed memory if none specified by user.  */

#if (WITH_HW)
  sim_hw_parse (sd, "/epiphany_mem");
  /* TODO: Need to be able to map external mem */
#else
  if (sim_core_read_buffer (sd, NULL, read_map, &c, 0, 1) == 0)
    sim_do_commandf (sd, "memory region 0,0x%x", EPIPHANY_DEFAULT_MEM_SIZE);

  if (epiphany_add_ext_mem)
    {
      if (sim_core_read_buffer (sd, NULL, read_map, &c,
				EPIPHANY_DEFAULT_EXT_MEM_BANK0_START, 1) == 0)
	sim_do_commandf (sd, "memory region 0x%x,0x%x",
			 EPIPHANY_DEFAULT_EXT_MEM_BANK0_START,
			 EPIPHANY_DEFAULT_EXT_MEM_BANK_SIZE);

      if (sim_core_read_buffer (sd, NULL, read_map, &c,
				EPIPHANY_DEFAULT_EXT_MEM_BANK1_START, 1) == 0)
	sim_do_commandf (sd, "memory region 0x%x,0x%x",
			 EPIPHANY_DEFAULT_EXT_MEM_BANK1_START,
			 EPIPHANY_DEFAULT_EXT_MEM_BANK_SIZE);

    }
#endif /* (WITH_HW) */

  /* check for/establish the reference program image */
  if (sim_analyze_program (sd,
			   (STATE_PROG_ARGV (sd) != NULL
			    ? *STATE_PROG_ARGV (sd)
			    : NULL),
			   abfd) != SIM_RC_OK)
    {
      free_state (sd);
      return 0;
    }

  /* Establish any remaining configuration options.  */
  if (sim_config (sd) != SIM_RC_OK)
    {
      free_state (sd);
      return 0;
    }

  if (sim_post_argv_init (sd) != SIM_RC_OK)
    {
      free_state (sd);
      return 0;
    }

  /* Open a copy of the cpu descriptor table.  */
  {
    CGEN_CPU_DESC cd = epiphany_cgen_cpu_open_1 (STATE_ARCHITECTURE (sd)->printable_name,
					     CGEN_ENDIAN_LITTLE);
    for (i = 0; i < MAX_NR_PROCESSORS; ++i)
      {
	SIM_CPU *cpu = STATE_CPU (sd, i);
	CPU_CPU_DESC (cpu) = cd;
	CPU_DISASSEMBLER (cpu) = sim_cgen_disassemble_insn;
      }
    epiphany_cgen_init_dis (cd);
  }

  /* Initialize various cgen things not done by common framework.
     Must be done after epiphany_cgen_cpu_open.  */
  cgen_init (sd);

  for (c = 0; c < MAX_NR_PROCESSORS; ++c)
    {
      /* Only needed for profiling, but the structure member is small.  */
      memset (CPU_EPIPHANY_MISC_PROFILE (STATE_CPU (sd, i)), 0,
	      sizeof (* CPU_EPIPHANY_MISC_PROFILE (STATE_CPU (sd, i))));
      /* Hook in callback for reporting these stats */
      PROFILE_INFO_CPU_CALLBACK (CPU_PROFILE_DATA (STATE_CPU (sd, i)))
	= print_epiphany_misc_cpu;
    }

  /* Store in a global so things like sparc32_dump_regs can be invoked
     from the gdb command line.  */
  current_state = sd;
  is_sim_opened = 1; /* To distinguish between HW and simulator target.  */

  SIM_CPU *current_cpu = STATE_CPU (sd, 0);
  cgen_init_accurate_fpu (current_cpu, CGEN_CPU_FPU (current_cpu),
			  epiphany_fpu_error);

  return sd;
}

void
sim_close (sd, quitting)
     SIM_DESC sd;
     int quitting;
{
  epiphany_cgen_cpu_close (CPU_CPU_DESC (STATE_CPU (sd, 0)));
  sim_module_uninstall (sd);
  free_state(sd);
}

SIM_RC
sim_create_inferior (sd, abfd, argv, envp)
     SIM_DESC sd;
     struct bfd *abfd;
     char **argv;
     char **envp;
{
  SIM_CPU *current_cpu = STATE_CPU (sd, 0);
  SIM_ADDR addr;

  if (abfd != NULL)
    addr = bfd_get_start_address (abfd);
  else
    addr = 0;
  sim_pc_set (current_cpu, addr);

#ifdef EPIPHANY_LINUX
  epiphanybf_h_cr_set (current_cpu,
		       epiphany_decode_gdb_ctrl_regnum(SPI_REGNUM), 0x1f00000);
  epiphanybf_h_cr_set (current_cpu,
		       epiphany_decode_gdb_ctrl_regnum(SPU_REGNUM), 0x1f00000);
#endif

#if 0
  STATE_ARGV (sd) = sim_copy_argv (argv);
  STATE_ENVP (sd) = sim_copy_argv (envp);
#endif

#if WITH_EMESH_SIM
  if (!es_valid_coreid(STATE_ESIM(sd), STATE_ESIM(sd)->coreid))
    {
      if (STATE_OPEN_KIND (sd) == SIM_OPEN_STANDALONE)
	sim_io_eprintf(sd, "Invalid coreid. Set with --coreid");
      else
	sim_io_eprintf(sd, "Invalid coreid. Set with \"sim coreid\"");
      return SIM_RC_FAIL;
    }
  /* Set coreid in cpu register
   * TODO: coreid register offset is hardcoded.
   */
  epiphanybf_h_coremesh_registers_set(STATE_CPU(sd, 0), 1,
				      STATE_ESIM(sd)->coreid);
  sim_io_eprintf(sd, "ESIM: Waiting for other cores...");
  /* TODO: Would be nice to support Ctrl-C here */
  es_wait_run(STATE_ESIM(sd));
  sim_io_eprintf(sd, " done.\n");
#endif

  return SIM_RC_OK;
}

/* PROFILE_CPU_CALLBACK */

static void
print_epiphany_misc_cpu (SIM_CPU *cpu, int verbose)
{
  SIM_DESC sd = CPU_STATE (cpu);
  char buf[20];

  if (CPU_PROFILE_FLAGS (cpu) [PROFILE_INSN_IDX])
    {
      sim_io_printf (sd, "Miscellaneous Statistics\n\n");
      sim_io_printf (sd, "  %-*s %s\n\n",
		     PROFILE_LABEL_WIDTH, "Fill nops:",
		     sim_add_commas (buf, sizeof (buf),
				     CPU_EPIPHANY_MISC_PROFILE (cpu)->fillnop_count));
    }
}