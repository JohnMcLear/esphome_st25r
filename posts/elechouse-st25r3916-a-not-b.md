# The Module That Lied to Me (Kind Of)

*A video script — written as a story*

---

**[OPEN on a desk. Components scattered everywhere. An ESP32-C6 dev board,
a small NFC module on a breakout, a half-finished coffee. The narrator speaks
over a slow zoom into the PCB.]*

---

So, this starts the way most of my rabbit holes start.

I'm deep in an ESPHome project — home automation stuff, nothing exotic — and I
want a solid NFC reader. Not one of those PN532 boards that barely works past
two centimetres. I want range. I want reliability. I want something that feels
like it was engineered instead of thrown together.

And someone on a Discord server drops a link to the Elechouse ST25R3916 module.

**[Cut to a product listing page.]*

The listing says *"ST25R3916B"*. The **B**. As in the improved revision. As in
the version STMicroelectronics released specifically to fix the known thermal
issues with the original A-silicon *and* add Automatic Wave Shaping — AWS —
which is this clever hardware feature that cleans up the RF waveform edge
transitions automatically, reducing emissions and improving compatibility with
picky cards.

I read the feature list. I read the datasheet. I order two of them.

---

**[Time-lapse: wiring a breadboard. Jumpers going in.]*

A week later the modules show up. They look exactly like the product photos —
that compact little PCB, the integrated coil antenna, the six-pin header.
Wilson Shen at Elechouse makes a nice board. I won't take that away from them.

I get it wired up to the ESP32-C6. SPI. CLK on 19, MISO on 10, MOSI on 18, CS
on 6, IRQ on 7. I'd been writing a custom ESPHome component around the
ST25R3916 at this point, so I knew the register map pretty well.

First thing any good driver does: read the IC Identity Register. That's
register `0x3F`. Bits 7 through 3 tell you which chip you're actually talking
to.

For the ST25R3916 — the original, the A — those bits should decode to `0x28`.

For the **B** revision — the one on the listing, the one I ordered — they
should decode to `0x30`.

I flash the firmware, open the serial monitor, and watch the boot log scroll
past.

---

**[Dramatic pause. Close-up on a terminal window.]*

```
[D][st25r:042]: IC Identity: 0x2A
[D][st25r:043]: Chip: ST25R3916 (A-variant)
```

Hm.

`0x2A`. Mask off the lower three bits. `0x2A & 0xF8 = 0x28`.

That's the A.

I stare at it for a second. Then I pull up the second module. Flash it.

```
[D][st25r:042]: IC Identity: 0x29
[D][st25r:043]: Chip: ST25R3916 (A-variant)
```

Also `0x28` family.

---

**[Cut to narrator at desk, leaning back.]*

Okay. So either I've made a mistake somewhere in the identity check, or these
modules are not what the listing says they are.

I go back to the datasheet — DS12484, Rev 8, the one for the original
ST25R3916 — and I double-check. Bits 7:3 of register `0x3F`. Yes. `00101` is
`0x28`. That's the A.

Then I pull up the ST25R3916B datasheet — DS13483 — and I look at the same
table. Bits 7:3 for the B are `00110`. That's `0x30`.

My modules are talking back with `0x28`. Not `0x30`.

---

**[Screen recording: scrolling through the STMicroelectronics community forum.]*

At this point I start digging. And here's where it gets interesting.

There's a thread on the ST Community Forum about initialization differences
between the two chip variants. And buried in the replies is something that
catches my eye — a note about **premature overheat protection**.

The original ST25R3916 — not the B — has a silicon quirk where the internal
thermal protection can fire before the chip actually gets hot. You have to
work around it in firmware: after power-on and the Set Default command, you
write `0x10` to register `0x04` using the Test Access command prefix `0xFC`.
It's in the application note. AN5584. Section 2.4.

The B revision? Doesn't need that fix. Never did. AWS instead. RC calibration
command `0xEA` on startup instead. Completely different initialization path.

So if you tried to initialize your "B" module as a B — skipping the overheat
fix, running RC cal — you might get subtle misbehaviour and not know why.

---

**[Back to narrator.]*

I sent an email to Elechouse.

I wasn't angry about it, genuinely just wanted to know. Asked them directly:
what silicon is in the module you're selling as ST25R3916B?

Wilson Shen replied. Quickly, actually. And he confirmed it.

The modules — at least at the time of my order, possibly for a while — contain
**ST25R3916 A-variant silicon**.

Not the B.

Now, to be fair: Wilson was straightforward about it. He didn't dodge the
question. The likely explanation is supply chain — the A was available, the B
was not, the board design works with either, the pinout is identical, and
someone made a sourcing decision that didn't make it back to the product
listing in any obvious way.

Is it malicious? Probably not. Is it misleading? Yeah, a bit. Especially if
you specifically needed AWS.

---

**[Cut to a side-by-side comparison table on screen.]*

Here's the practical difference, because I want to be concrete about this:

| Feature | ST25R3916 (A) | ST25R3916B |
|---|---|---|
| AWS (Auto Wave Shaping) | ❌ No | ✅ Yes |
| Overheat fix needed | ✅ Yes (`0xFC/0x04/0x10`) | ❌ No |
| RC Calibration command | ❌ N/A | ✅ `0xEA` on startup |
| IC Identity bits 7:3 | `0x28` | `0x30` |
| General NFC reading | ✅ Works great | ✅ Works great |

For most home automation use-cases — reading an Ultralight tag to unlock a
door, scanning a credit card to log it, that kind of thing — the A works
perfectly. ISO14443A, anticollision, cascade levels, all of it. The A is a
great chip.

The difference only matters if you're worried about RF spectral compliance in
a product you're certifying, or if you genuinely need the tighter waveform
shaping for edge-case tag compatibility.

---

**[Narrator, looking at the module.]*

So I adapted the component. The identity check now masks correctly and branches
the initialization path. If it sees `0x28` family, it applies the overheat
fix. If it ever sees `0x30` family — a real B — it skips that and runs RC cal.

And for anyone who actually *does* need the B silicon, we designed a custom
PCB. Forty by forty millimetres. Integrated two-turn antenna. Uses the real
ST25R3916B from JLCPCB — part number C17315217, if you want to look it up.
Drop-in compatible pinout with the Elechouse module so you don't have to
change your wiring.

That design is in the `pcb/` folder of the repository.

---

**[Closing shot. The ESP32 board with the NFC module, a card laid on top of
the antenna, terminal showing a successful tag read.]*

The tag still reads. The distance is still good. The A silicon is fine.

But you should know what you have.

Check your IC Identity Register. If it's `0x28` family, initialize it as an A.
If it's `0x30` family, you have an actual B.

And if the listing said B and you got A — well. Now you know.

---

*[END CARD with repo URL and component name]*

---

> **Repository:** [JohnMcLear/esphome_st25r](https://github.com/JohnMcLear/esphome_st25r)  
> If you need the ST25R3916B silicon, see the custom PCB design in `pcb/`.
