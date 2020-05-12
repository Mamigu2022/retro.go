/*
** Nofrendo (c) 1998-2000 Matthew Conte (matt@conte.com)
**
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of version 2 of the GNU Library General
** Public License as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** Library General Public License for more details.  To obtain a
** copy of the GNU Library General Public License, write to the Free
** Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** Any permitted reproduction of these routines, in whole or in part,
** must bear this legend.
**
**
** nes_mem.c
**
** Memory related functions
** by ducalex
*/

#include <string.h>
#include <noftypes.h>
#include <nofrendo.h>
#include <nes_input.h>
#include <osd.h>
#include <nes.h>

#include <esp_attr.h>

static mem_t mem;


static void write_protect(uint32 address, uint8 value)
{
   /* don't allow write to go through */
   UNUSED(address);
   UNUSED(value);
}

/* read/write handlers for standard NES */
static mem_read_handler_t read_handlers[] =
{
   { 0x2000, 0x3FFF, ppu_read   },
   { 0x4000, 0x4015, apu_read   },
   { 0x4016, 0x4017, input_read },
   LAST_MEMORY_HANDLER
};

static mem_write_handler_t write_handlers[] =
{
   { 0x2000, 0x3FFF, ppu_write   },
   { 0x4014, 0x4014, ppu_write   },
   { 0x4016, 0x4016, input_write },
   { 0x4000, 0x4017, apu_write   },
   { 0x8000, 0xFFFF, write_protect},
   LAST_MEMORY_HANDLER
};

/* Set 4KB memory page */
IRAM_ATTR void mem_setpage(uint16 page, uint8 *ptr)
{
   mem.pages[page] = ptr;

   if (!MEM_PAGE_HAS_HANDLERS(mem.pages_read[page]))
   {
      mem.pages_read[page] = ptr;
   }

   if (!MEM_PAGE_HAS_HANDLERS(mem.pages_write[page]))
   {
      mem.pages_write[page] = ptr;
   }
}

/* Get 4KB memory page */
uint8 *mem_getpage(uint16 page)
{
   return mem.pages[page];
}

void mem_setmapper(mapintf_t *intf)
{
   int num_read_handlers = 0, num_write_handlers = 0;

   memset(&mem.read_handlers, 0, sizeof(mem.read_handlers));
   memset(&mem.write_handlers, 0, sizeof(mem.write_handlers));

   // Reset our map to pages set via mem_setpage
   for (int i = 0; i < MEM_PAGECOUNT; i++)
   {
      mem.pages_read[i]  = mem.pages[i];
      mem.pages_write[i] = mem.pages[i];
   }

   // Special MMC handlers
   if (intf != NULL)
   {
      for (int i = 0, rc = 0, wc = 0; i < MEM_HANDLERS_MAX; i++)
      {
         if (intf->mem_read && intf->mem_read[rc].read_func)
            memcpy(&mem.read_handlers[num_read_handlers++], &intf->mem_read[rc++],
                  sizeof(mem_read_handler_t));

         if (intf->mem_write && intf->mem_write[wc].write_func)
            memcpy(&mem.write_handlers[num_write_handlers++], &intf->mem_write[wc++],
                  sizeof(mem_write_handler_t));
      }
   }

   // Special IO handlers
   for (int i = 0, rc = 0, wc = 0; i < MEM_HANDLERS_MAX; i++)
   {
      if (read_handlers[rc].read_func)
         memcpy(&mem.read_handlers[num_read_handlers++], &read_handlers[rc++],
               sizeof(mem_read_handler_t));

      if (write_handlers[wc].write_func)
         memcpy(&mem.write_handlers[num_write_handlers++], &write_handlers[wc++],
               sizeof(mem_write_handler_t));
   }

   // Mark pages if they contain handlers (used for fast access in nes6502)
   for (mem_read_handler_t *mr = mem.read_handlers; mr->read_func != NULL; mr++)
   {
      for (int i = mr->min_range; i < mr->max_range; i++)
         mem.pages_read[i >> MEM_PAGESHIFT] = MEM_PAGE_USE_HANDLERS;
   }

   for (mem_write_handler_t *mw = mem.write_handlers; mw->write_func != NULL; mw++)
   {
      for (int i = mw->min_range; i < mw->max_range; i++)
         mem.pages_write[i >> MEM_PAGESHIFT] = MEM_PAGE_USE_HANDLERS;
   }

   ASSERT(num_read_handlers <= MEM_HANDLERS_MAX);
   ASSERT(num_write_handlers <= MEM_HANDLERS_MAX);
}

/* read a byte of 6502 memory space */
IRAM_ATTR uint8 mem_getbyte(uint32 address)
{
   uint8 *page = mem.pages_read[address >> MEM_PAGESHIFT];

   /* Special memory handlers */
   if (MEM_PAGE_HAS_HANDLERS(page))
   {
      for (mem_read_handler_t *mr = mem.read_handlers; mr->read_func != NULL; mr++)
      {
         if (address >= mr->min_range && address <= mr->max_range)
            return mr->read_func(address);
      }
      page = mem.pages[address >> MEM_PAGESHIFT];
   }

   /* Unmapped region */
   if (page == NULL)
   {
      MESSAGE_DEBUG("Read unmapped region: $%4X\n", address);
      return 0xFF;
   }
   /* RAM */
   else if (address < 0x2000)
   {
      return page[address & MEM_RAMMASK];
   }
   /* Paged memory */
   else
   {
      return page[address & MEM_PAGEMASK];
   }
}

/* write a byte of data to 6502 memory space */
IRAM_ATTR void mem_putbyte(uint32 address, uint8 value)
{
   uint8 *page = mem.pages_write[address >> MEM_PAGESHIFT];

   /* Special memory handlers */
   if (MEM_PAGE_HAS_HANDLERS(page))
   {
      for (mem_write_handler_t *mw = mem.write_handlers; mw->write_func != NULL; mw++)
      {
         if (address >= mw->min_range && address <= mw->max_range)
         {
            mw->write_func(address, value);
            return;
         }
      }
      page = mem.pages[address >> MEM_PAGESHIFT];
   }

   /* Unmapped region */
   if (page == NULL)
   {
      MESSAGE_DEBUG("Write to unmapped region: $%2X to $%4X\n", address, value);
   }
   /* RAM */
   else if (address < 0x2000)
   {
      page[address & MEM_RAMMASK] = value;
   }
   /* Paged memory */
   else
   {
      page[address & MEM_PAGEMASK] = value;
   }
}

IRAM_ATTR uint32 mem_getword(uint32 address)
{
   return mem_getbyte(address + 1) << 8 | mem_getbyte(address);
}

void mem_reset()
{
   memset(&mem.ram, 0, MEM_RAMSIZE);
}

mem_t *mem_create()
{
   memset(&mem, 0, sizeof(mem));

   mem_setpage(0, mem.ram);
   mem_setpage(1, mem.ram);
   mem_setmapper(NULL);

   return &mem;
}

void mem_destroy(mem_t *mem)
{
   //
}