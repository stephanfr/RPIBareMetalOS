// TEMPORARY DIAGNOSTIC -- delete once RPi5 boot is working.
//
// Both entry points run with the MMU and caches OFF, which is mandatory:
// with the MMU on, mailbox messages in cacheable memory are never seen by the GPU.

#include <stdint.h>

#include "asm_globals.h"
#include "devices/video/fonts/basic_8x8.h"

namespace
{
    constexpr uintptr_t RPI5_MAILBOX_BASE = 0x107C013880ULL;

    constexpr uint32_t MBOX_READ_OFFSET   = 0x00;
    constexpr uint32_t MBOX_STATUS_OFFSET = 0x18;
    constexpr uint32_t MBOX_WRITE_OFFSET  = 0x20;

    constexpr uint32_t MBOX_STATUS_FULL      = 0x80000000u;
    constexpr uint32_t MBOX_STATUS_EMPTY     = 0x40000000u;
    constexpr uint32_t MBOX_RESPONSE_SUCCESS = 0x80000000u;

    volatile uint32_t &MailboxRegister(uint32_t offset)
    {
        return *reinterpret_cast<volatile uint32_t *>(RPI5_MAILBOX_BASE + offset);
    }

    uint64_t ReadCounterTicks()
    {
        uint64_t ticks;
        asm volatile("mrs %0, cntpct_el0" : "=r"(ticks));
        return ticks;
    }

    uint64_t TicksPerSecond()
    {
        uint64_t freq;
        asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
        return freq ? freq : 1;
    }

    bool RawMailboxCall(volatile uint32_t *mbox, uint8_t channel)
    {
        uint32_t message_addr = (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mbox)) & ~0xFu) | (channel & 0xFu);

        asm volatile("dsb sy" ::: "memory");

        uint64_t deadline = ReadCounterTicks() + TicksPerSecond() * 2;

        while (MailboxRegister(MBOX_STATUS_OFFSET) & MBOX_STATUS_FULL)
        {
            if (ReadCounterTicks() >= deadline) return false;
        }

        MailboxRegister(MBOX_WRITE_OFFSET) = message_addr;

        while (true)
        {
            while (MailboxRegister(MBOX_STATUS_OFFSET) & MBOX_STATUS_EMPTY)
            {
                if (ReadCounterTicks() >= deadline) return false;
            }

            uint32_t response = MailboxRegister(MBOX_READ_OFFSET);

            if ((response & 0xF) != channel) continue;

            asm volatile("dsb sy" ::: "memory");

            return mbox[1] == MBOX_RESPONSE_SUCCESS;
        }
    }

    //  ---- text output ----

    volatile uint8_t *g_fb = nullptr;
    uint32_t g_pitch           = 0;
    uint32_t g_bytes_per_pixel = 4;
    uint32_t g_cursor_x = 0;
    uint32_t g_cursor_y = 0;

    void PutPixel(uint32_t x, uint32_t y, uint32_t rgb)
    {
        volatile uint8_t *p = g_fb + static_cast<uint64_t>(y) * g_pitch + static_cast<uint64_t>(x) * g_bytes_per_pixel;

        if (g_bytes_per_pixel == 2)
        {
            uint16_t r = static_cast<uint16_t>((rgb >> 19) & 0x1F);
            uint16_t g = static_cast<uint16_t>((rgb >> 10) & 0x3F);
            uint16_t b = static_cast<uint16_t>((rgb >>  3) & 0x1F);

            *reinterpret_cast<volatile uint16_t *>(p) = static_cast<uint16_t>((r << 11) | (g << 5) | b);
        }
        else
        {
            *reinterpret_cast<volatile uint32_t *>(p) = rgb;
        }
    }

    void DrawChar(char c)
    {
        if (c == '\n')
        {
            g_cursor_x = 0;
            g_cursor_y += 10;
            return;
        }

        unsigned char ch = static_cast<unsigned char>(c);

        if (ch >= 128) ch = '?';

        for (uint32_t row = 0; row < 8; row++)
        {
            unsigned char bits = font8x8_basic[ch][row];

            for (uint32_t col = 0; col < 8; col++)
            {
                if (bits & (1u << col))
                {
                    PutPixel(g_cursor_x + col, g_cursor_y + row, 0x00FFFFFFu);
                }
            }
        }

        g_cursor_x += 9;
    }

    void DrawString(const char *s)
    {
        while (*s) DrawChar(*s++);
    }

    void DrawUInt(uint32_t v)
    {
        char buf[12];
        int  n = 0;

        if (v == 0) { DrawChar('0'); return; }

        while (v && n < 12) { buf[n++] = static_cast<char>('0' + (v % 10)); v /= 10; }
        while (n) DrawChar(buf[--n]);
    }

    void DrawHex(uint32_t v)
    {
        DrawString("0x");

        for (int shift = 28; shift >= 0; shift -= 4)
        {
            uint32_t nib = (v >> shift) & 0xF;
            DrawChar(nib < 10 ? static_cast<char>('0' + nib) : static_cast<char>('A' + nib - 10));
        }
    }

    volatile uint32_t mbox_buf[40] __attribute__((aligned(16)));

    bool QueryPhysicalSize(uint32_t &width, uint32_t &height)
    {
        int i = 0;
        mbox_buf[i++] = 0;
        mbox_buf[i++] = 0;
        mbox_buf[i++] = 0x00040003u;    // GET_PHYSICAL_WIDTH_HEIGHT
        mbox_buf[i++] = 8;
        mbox_buf[i++] = 0;
        mbox_buf[i++] = 0;
        mbox_buf[i++] = 0;
        mbox_buf[i++] = 0;

        mbox_buf[0] = static_cast<uint32_t>(i * 4);

        if (!RawMailboxCall(mbox_buf, 8)) return false;

        width  = mbox_buf[5];
        height = mbox_buf[6];

        return width != 0 && height != 0;
    }

    //  Allocates (or re-allocates) the framebuffer and prepares the text
    //      renderer.  Returns false if the mailbox does not cooperate.

    bool SetupFramebufferForText(uint32_t &width, uint32_t &height,
                                 uint32_t &pitch, uint32_t &depth,
                                 uint32_t &fb_addr_raw, uint32_t &fb_size)
    {
        uint32_t native_width  = 0;
        uint32_t native_height = 0;

        if (!QueryPhysicalSize(native_width, native_height))
        {
            native_width  = 1024;
            native_height = 768;
        }

        int i = 0;
        mbox_buf[i++] = 0;
        mbox_buf[i++] = 0;

        int phys_tag_index = i;
        mbox_buf[i++] = 0x00048003u;    // SET_PHYS_WH
        mbox_buf[i++] = 8;
        mbox_buf[i++] = 0;
        mbox_buf[i++] = native_width;
        mbox_buf[i++] = native_height;

        mbox_buf[i++] = 0x00048004u;    // SET_VIRT_WH
        mbox_buf[i++] = 8;
        mbox_buf[i++] = 0;
        mbox_buf[i++] = native_width;
        mbox_buf[i++] = native_height;

        mbox_buf[i++] = 0x00048009u;    // SET_VIRT_OFFSET
        mbox_buf[i++] = 8;
        mbox_buf[i++] = 0;
        mbox_buf[i++] = 0;
        mbox_buf[i++] = 0;

        int depth_tag_index = i;
        mbox_buf[i++] = 0x00048005u;    // SET_DEPTH
        mbox_buf[i++] = 4;
        mbox_buf[i++] = 0;
        mbox_buf[i++] = 32;

        mbox_buf[i++] = 0x00048006u;    // SET_PIXEL_ORDER
        mbox_buf[i++] = 4;
        mbox_buf[i++] = 0;
        mbox_buf[i++] = 1;

        int allocate_tag_index = i;
        mbox_buf[i++] = 0x00040001u;    // ALLOCATE_BUFFER
        mbox_buf[i++] = 8;
        mbox_buf[i++] = 0;
        mbox_buf[i++] = 4096;
        mbox_buf[i++] = 0;

        int pitch_tag_index = i;
        mbox_buf[i++] = 0x00040008u;    // GET_PITCH
        mbox_buf[i++] = 4;
        mbox_buf[i++] = 0;
        mbox_buf[i++] = 0;

        mbox_buf[i++] = 0;              // LAST

        mbox_buf[0] = static_cast<uint32_t>(i * 4);

        if (!RawMailboxCall(mbox_buf, 8)) return false;

        fb_addr_raw = mbox_buf[allocate_tag_index + 3];
        fb_size     = mbox_buf[allocate_tag_index + 4];
        pitch       = mbox_buf[pitch_tag_index + 3];
        width       = mbox_buf[phys_tag_index + 3];
        height      = mbox_buf[phys_tag_index + 4];
        depth       = mbox_buf[depth_tag_index + 3];

        if (fb_addr_raw == 0 || pitch == 0 || width == 0 || height == 0) return false;

        uint32_t derived_bpp = pitch / width;

        g_fb              = reinterpret_cast<volatile uint8_t *>(static_cast<uintptr_t>(fb_addr_raw & 0x3FFFFFFFu));
        g_pitch           = pitch;
        g_bytes_per_pixel = (derived_bpp == 2 || derived_bpp == 4) ? derived_bpp : 4;

        for (uint64_t offset = 0; offset + 4 <= static_cast<uint64_t>(pitch) * height; offset += 4)
        {
            *reinterpret_cast<volatile uint32_t *>(g_fb + offset) = 0;
        }

        g_cursor_x = 0;
        g_cursor_y = 0;

        return true;
    }
}

//  Called from the top of _start -- proves the image is executing and the
//      mailbox/HDMI path works.

extern "C" void RPI5RawFramebufferDiagnostic()
{
    uint32_t width, height, pitch, depth, fb_addr_raw, fb_size;

    if (!SetupFramebufferForText(width, height, pitch, depth, fb_addr_raw, fb_size)) return;

    DrawString("RPI5 FB DIAG v2\n\n");
    DrawString("APPLIED ");  DrawUInt(width); DrawChar('x'); DrawUInt(height); DrawChar('\n');
    DrawString("PITCH ");    DrawUInt(pitch);              DrawChar('\n');
    DrawString("DEPTH ");    DrawUInt(depth);              DrawChar('\n');
    DrawString("BYTES/PX "); DrawUInt(pitch / width);      DrawChar('\n');
    DrawString("ADDR ");     DrawHex(fb_addr_raw);         DrawChar('\n');
    DrawString("SIZE ");     DrawHex(fb_size);             DrawChar('\n');
}

//  Called immediately after GetBootTimeSettings in start.S, MMU still off.
//      If this text appears, GetBootTimeSettings did NOT hang, and the values
//      shown are exactly what AARCH64PlatformMemoryManager's constructor is
//      about to act on -- MEMFIELD must be 2, 4 or 5 or that constructor calls
//      ParkCore() and the boot dies silently right there.

extern "C" void RPI5BootValuesDiagnostic()
{
    uint32_t width, height, pitch, depth, fb_addr_raw, fb_size;

    if (!SetupFramebufferForText(width, height, pitch, depth, fb_addr_raw, fb_size)) return;

    uint32_t memory_field = (__board_version >> 20) & 0x07;

    DrawString("RPI5 BOOT VALUES\n\n");
    DrawString("BOARDVER ");  DrawHex(__board_version);                   DrawChar('\n');
    DrawString("MEMFIELD ");  DrawUInt(memory_field);
    DrawString(memory_field == 2 || memory_field == 4 || memory_field == 5 ? "  (handled)\n" : "  *** PARKS ***\n");
    DrawString("VCBASE ");    DrawHex(__videocore_memory_base);           DrawChar('\n');
    DrawString("VCSIZE ");    DrawHex(__videocore_memory_size_in_bytes);  DrawChar('\n');
}