# Faxanadu DX

A remaster of Faxanadu (NES) that keeps as much of the real game as possible,
and fixes what has aged.

## Goals

- **The real game underneath.** It runs Faxanadu's own code, built from your
  ROM, so the world, music, story and feel are the original's. The remaster
  steers the game from outside; it doesn't rewrite it.
- **A Metroidvania at heart.** Faxanadu is one connected world that opens up
  as you find keys, tools and the way through, and it keeps sending you back
  to places you have been. The remaster leans into that: one continuous
  world with no stop to scroll between rooms, a map of where you have been
  and what still lies there, Guru travel to get back quickly, and upgrades
  that reward a return trip.
- **Quality of life.** Conveniences the 1987 game lacks, like saving without
  writing down mantras and travelling between Gurus. Where the game can offer
  them itself, it does, in its own text box and voice.
- **Combat, tuned with care.** Combat may be adjusted to be a bit more fun,
  without losing what makes Faxanadu feel like Faxanadu.
- **The original, one switch away.** Everything below is one mod, Faxanadu
  DX, on by default. Switch it off in the launcher's Mods screen and you play
  the original game, mantras and all.

How the game works, as far as we've learned it, is in
[docs/GAME_INTERNALS.md](docs/GAME_INTERNALS.md).

## What's new

### Seamless: one continuous world

The remaster plays in Seamless. Faxanadu's rooms join into one continuous
world: no more stopping to scroll at each screen's edge, the next room is
already in view, and its people and monsters keep moving. The view is always
424x240, the original's full height and 53 tiles wide, scaled to your window.
F4 shows the region you're in. Doors and region changes keep the original's
own transitions, and so does each boss's room: while its boss lives, the
room stays apart and you fight on that one screen, as in the original; once
the boss is dead, the room joins the world again. Switch the remaster off and
the original's own 256-wide view comes back with the rest of the game. See
[docs/SEAMLESS-WORLD.md](docs/SEAMLESS-WORLD.md).

![The same spot in the original and in the remaster, which joins the next screen on](docs/screenshots/seamless.png)

### A quest log

Faxanadu's people tell you where to go and what to find, but nothing says
which lines matter, and they are easy to forget. Now when someone asks
something of you, the text box adds "New quest" and its name after their
line, and "Quest complete" when it is done: the King's plea for the
fountain's water, the Ring of Elf, the Mattock and the wall, the three
springs, the path to Mascon, the meteorite, the Battle Suit, the rings of
Dworf and the Demon, and the Evil One, 17 in all. Chatter stays chatter.
Press L, or the left trigger on a controller, and the game pauses on your
quests, the music playing on: the ones under way with what to do, then the
ones done. Your file keeps them. The Select menu lists QUESTS at its top, so
you can open the log from there too.

![The King's plea ends with New quest, The King's Plea, in his box; the quest log, partway to Mascon](docs/screenshots/quest-log.png)

A gold "!" floats over anyone with a quest for you, and a "?" over the one
you should see next.

![Before you speak to the King, a "!" over him; after, none. The man on Mist who sent you for the meteorite, with a "?" now you have it](docs/screenshots/quest-marks.png)

### The map

Press M, or Back on a controller, and the game pauses on a map of where you
have been, as in Metroid: a box for every screen you have stood on, laid out
by how the screens join, with the one you are on blinking. Screens you haven't
seen stay blank. Each region, tower and castle has its page, the towns sitting
between the screens they join; Left and Right turn the page, and B, Start, M
or Back close the map. It marks only what matters: a cross for each Guru you
have visited, a block for every other door you have gone through, and a dot
wherever an item still lies on a screen you have seen, gone once you pick it
up (one that turns up only now and then shows only while it is there). Ladders
you have climbed join the screens above and below. Your file keeps the map,
and the items you have taken. MAP, under QUESTS at the top of the Select menu,
opens it too. The game holds still underneath, but the music plays on.

![The map on the Trunk's page: the screens seen from its first past Forepaw, with the towns between them, the crosses of two Gurus, four doors used, the dots of two items still lying there, and the hero's screen lit](docs/screenshots/map.png)

## Also new

### Saving and the Gurus

- **Save files.** Three files, as in Zelda. The title has NEW GAME over
  CONTINUE: NEW GAME asks which file to start in, and asks before erasing a
  used one, and CONTINUE picks a saved file and continues it exactly as you
  left it. A Guru no longer gives a mantra: he offers to remember your
  journey, and you continue at the last Guru you saved with.
- **Guru travel.** A temple's Guru can send you to any other Guru you have
  visited, keeping your HP, MP and gold.
- **Zone respawn.** After a death you come back where you last entered the
  tower, castle, town or region you died in, instead of at a temple.
- **Death keeps gold.** The Guru brings you back with all your gold and
  experience, instead of your title's starting amounts.

### Combat

- **Block.** Hold V, or the right trigger on a controller, with a shield or
  the Battle Helmet to raise it and stop enemy magic from the front, and
  Sugata's flash from any side. You glint in the shield's colors as you raise
  it.
- **Spell cost on the MP bar.** As in Castlevania: Belmont's Curse, the end
  of the M: bar lights a shade brighter for what your selected spell will
  cost, so you see what a cast leaves you. Short of MP for it, the whole bar
  goes a shade darker: the spell won't go off.
- **Magic button.** C, or Y on a controller, casts your selected spell, as
  Up+B does, but without going through a door, talking to anyone or swinging
  your sword.
- **MP from kills.** As hearts do in Castlevania, every enemy you kill, by
  sword or spell, gives MP back: a little for the weak ones, more for the
  tough, 40 for a boss. It no longer comes back by waiting, and talking to
  any Guru fills your HP and MP. Eolis's Magician no longer sells MP: once
  you have learned Fire at the magic hall in the Mist, he teaches you, once,
  for 30000 Golds, to draw twice as much from every kill, which keeps the
  Wing Boots and the Hour Glass going longer too.
- **Damage numbers.** Every hit that takes HP shows how much, floating up from
  where it landed: white for the damage you deal, pink for the damage you take.
- **A taller dagger.** The Hand Dagger's swing reaches as low as the swords',
  so it hits low enemies like the Zozura outside Eolis. It keeps its short
  reach.
- **Sugata's flash, made fair.** Sugata, in the Mist, blinks before it flashes
  and dims the band of the screen the flash will reach, and the flash hurts
  only there, where it used to hit you anywhere, Ointment or not. With the
  Magic Shield, Block turns it back on Sugata.
- **Weapons indoors.** In shops, houses and temples the Select menu equips
  what you choose and you keep your weapon in hand, and your sword hurts the
  few enemies indoors, in the Mist and Dartmoor.
- **Fair entrances.** Some enemies stood right where a door, a ladder, a
  screen's edge or the bottom of a drop puts you, and hit you before you could
  react. They start a little way off now; every other enemy is where the
  original puts it.

### Getting around

- **Forgiving jumps.** A pressed up to 5 frames after you walk off a ledge
  still jumps, and up to 6 frames before you land jumps as you land. Walking
  off a ledge shows the jump pose, instead of walking on air.
- **Holding on to ladders.** A hit while you're on a ladder no longer knocks
  you off it.
- **Zone names.** Entering a different town, region, tower or castle shows its
  name below the status bar for about two seconds.
- **A modern controller layout.** Start opens the Select menu and closes it,
  Back opens the map, the left trigger the quest log, B backs out of menus
  and text, and every action has a button of its own (see Controls).
  Nothing pauses the game but the screens that hold it anyway. A
  Nintendo-layout pad is read by where its buttons sit, so the lower one
  jumps, as on any other.

### Items and shops

- **Use item button.** S, or B on a controller, uses your selected item, as
  Down+B does, but without swinging your sword or climbing down a ladder.
  Like Down+B, it does nothing in the air, unless you're flying on the Wing
  Boots with A held.
- **Keys and tools.** Up at a locked door opens it with the right key from
  your inventory, without selecting it, and keys and the Mattock are no longer
  used up.
- **Items that run on MP.** The Wing Boots and the Hour Glass stay with you,
  and both run on MP, so you balance them against your spells. The Wing Boots
  take MP only while you hold A to fly: 40 seconds of flight on a full bar at
  first, up to 80 as your title rises; walking, standing or falling with them
  on costs nothing. The Hour Glass freezes enemies for its 15 seconds for
  40 MP, and no longer costs half your HP. Use either again to end it early;
  with no MP, they do nothing. Turning one on or off shows no text box, just
  its sound and the item box's gold brackets, so play never stops; when
  one ends without you, a short line below the status bar says why ("Out of
  magic"), and the first use of each explains it once. There's no cooldown:
  they work again as soon as you have MP, and T: on the status bar counts the
  selected one down. The Elixir stays with you after it revives you, and a
  Guru recharges it and tells you so; the first Guru you bring it to tells
  you he can.
- **Found, not bought.** Since the Wing Boots, the Hour Glass and the Elixir
  stay with you, no shop sells them any more: they're upgrades you find. The
  Wing Boots come only from the Dwarf at the top of the Tower of Fortress,
  the Hour Glass lies in the Tower of Suffer until you take it, and the
  Elixirs lie where they always did, one beside the Dwarf's Wing Boots. The
  Spring of Trunk needs a charged Elixir and uses up its charge, and a Guru
  restores it.
- **Ready at a glance.** The status bar shows your Elixir beside the M: and
  P: bars it refills, as Super Metroid shows a reserve tank beside Energy: in
  color when it is charged, grey while it waits for a Guru, and not at all
  until you have it. It greys and blinks the moment it revives you, so you
  see when it saves you. And while the Wing Boots or the Hour Glass in your
  item box is running, the box's brackets turn gold, and go back to white
  the moment it stops.
- **Quick shops.** Shopkeepers talk as they always did, but after you buy or
  sell you stay in the list until you're done, and B thanks you on your way
  out. Any shop buys what you carry, except what you own for good, like a key
  or the Mattock: it says OWNED and isn't for sale.
- **Potion Sellers.** The meat shops sell Red Potions instead of dried meat,
  each at the price its town's tool shop charged, and the tool shops no longer
  stock them. They still buy one back.
- **Double gold.** The King gives you 3000 Golds instead of 1500, counted out
  in the same 2.5 seconds, but only once: come back broke and he has nothing
  more for you. Every coin a monster drops is worth twice as much. Selling
  pays what it did.

### Text and the title

- **Faster text.** Hold A and text types 4 times as fast. A page break or a
  question still waits for a fresh press of A, so you never skip a page.
- **Speaker names.** Someone with a portrait is named on a tab above the
  dialogue box, as the game names them: "Guru of Apolune", "King of Eolis".
- **Slain by.** When something takes your last HP, a banner names it while you
  fall: "Slain by a Sugata", "Slain by Poison".
- **DX on the title.** The title screen shows DX on the logo, just under the
  FAXANADU letters, in the title's own letters and the logo's light yellow,
  outlined in black.

### The picture

- **No sprite flicker.** The NES shows 8 sprites a line, so Faxanadu swaps its
  sprites' order every frame, and beside the villagers you flickered behind
  them as if see-through. The remaster draws every sprite, every frame: you in
  front, then your spell, then everyone else, and a busy room no longer drops
  anyone for a frame when the game slows down. The Fire spell still flickers,
  as it was made to.

## Bugs fixed

The remaster also fixes these bugs in the original game:

- The Pendant was meant to make your weapon hit a quarter harder, but a
  reversed check gave that bonus to everyone without it, and finding the
  Pendant took it away. Now the bonus comes with the Pendant.
- A shot on your shield still let its share through and knocked you back when
  nothing should have hurt you: with Ointment, or in the second after a hit.
  Now the shield takes it, and you take nothing.
- Picking up the Battle Suit counted your weapons instead of your armor, and
  the Dragon Slayer your armor instead of your weapons, so either could wipe
  out what you carried or list what you didn't have. Now each joins its own
  list.
- Finding a Mattock, Wing Boots, an Hour Glass or a Red Potion with eight
  items listed said you got it, then lost it, and Ripasheiku's Mattock stayed
  gone until you died. Now the find stays where it lies, with a buzz, until
  you have room.
- Flying with the Wing Boots over the wall at Eolis's right edge, in the town
  or outside its gate, and letting go dropped you down a gap under the floor,
  stuck until a reset. Now the gap is as solid as the wall beside it.
- Holding Up at the top of a ladder bobbed you a pixel past its end and back
  for as long as you held it. Now you stop at the top, and A jumps from there.
- Stepping onto the dried fountain's screen and straight back off dropped the
  fountain's ladder into the next screen, through its floor. Now it drops only
  on its own screen.
- Five graphics glitches are drawn as they were meant to be: a stray tile on
  the hero's chest on a ladder in Studded Mail, a piece of the Fire spell's
  first frame in its second, the Myconid's foot flickering into the wrong
  colors, a wall block in the Tower of Fortress's floor, and the grey tip of
  the F in the title's logo.
- The typing sound read past the end of its own table, so every character
  ended in a loud clack. Now it plays the softer sound the table holds.

## Controls

| | Keyboard | Controller |
|---|---|---|
| Move | Arrow keys | D-pad or left stick |
| Jump (A) | Z | A |
| Attack (B) | X | X |
| Magic | C | Y |
| Use item (in play) | S | B |
| Back out of menus and text | X | B or X |
| Block (hold, with a shield) | V | Right trigger |
| Select menu | Enter | Start |
| Map | M | Back |
| Quest log | L | Left trigger |
| Rewind | F8 | Left shoulder |
| Fast-forward (hold) | Tab | Right shoulder |
| Menu (save states, rewind, controls, quit) | Escape | Right stick (press) |
| Save states | F7 | |
| Fullscreen | F11 | |
| Screenshot | F12 | |

A controller's buttons are named for where they sit, as on an Xbox pad: A is
the lower face button (✕ on a PlayStation pad), B the right (○), X the left
(□) and Y the top (△). A Nintendo-layout pad, like a Switch Pro or an 8BitDo
in Switch mode, is read the same way, by place rather than by its letters.

On a controller, B is Use item only in play. Everywhere else it backs out,
as a modern pad's B does: it is the NES's B in text and its questions, the
Select menu, its lists and the PLAYER page, shops, the map and the quest
log, Guru travel and saving, and the title's list of files, where X still
is too. Each press is one or the other, so a B that uses an item never
closes the line it brings up, and a B that closes a menu never uses an item
or swings the sword. On the keyboard, S is only ever Use item, and X the
NES's B.

Enter and Start are the NES's Start, and Backslash and Back its Select. In
play, Start opens the Select menu and closes it, and no longer pauses;
Backslash opens the menu too, and Back is the map's. Everywhere else they
are the NES's own: on the title, Select or the D-pad moves between NEW GAME
and CONTINUE, A or Start chooses, and B goes back from the list of files; in
a shop, Select switches between buying and selling. With the remaster off,
the pad is the original's: Start pauses, and Back opens the Select menu.

The launcher's Controls page remaps them, the remaster's five buttons after
the shortcuts. They are also in the `[Game]` section of `config.ini`, next to
the game: `MagicKey`, `UseItemKey`, `BlockKey`, `MapKey` and `QuestLogKey` (a
key name, like `V` or `Left Shift`), and `MagicButton`, `UseItemButton`,
`BlockButton`, `MapButton` and `QuestLogButton` (a controller button, like
`b` or `leftshoulder`, or a trigger, like `lefttrigger+`). `none` unbinds
one. Pick ones nothing else uses. A `config.ini` from an earlier version
moves to this layout the first time you start, except for any binding you
changed; a new default that would land on a button you gave something else
is left unbound instead, for you to place.

## Save files

Your three files are `faxanadu_file1.txt` to `faxanadu_file3.txt`, next to
the game. Block remembers that it has shown its line in `faxanadu_block.txt`
beside them.

The remaster has no mantras: it neither shows nor reads them. Switch it off
and the original's CONTINUE asks for one, as it always did.

## The ROM it needs

The remaster is built from your own copy of **Faxanadu (USA)**; it ships no
game data. The build checks the ROM and rejects any other, including **Faxanadu
(USA) (Rev A)**, a different revision.

| | ROM data only, iNES header excluded |
|-------|-------|
| CRC32 | `57DD23D1` |
| MD5   | `b3bb9296b5e1e0c83e90b6da9e8a41aa` |
| SHA-1 | `5b05c8859f356013d37f0545f5de5fa1693da5da` |

## Building and running

The remaster builds and plays on Linux and on Macs with Apple Silicon. You
need [Nix](https://nixos.org/download/): the flake's devShell (`flake.nix`)
has everything else the build needs, on both. With
[direnv](https://direnv.net/), `direnv allow` loads it whenever you enter the
repo; otherwise run the commands inside `nix develop`. Put your ROM at the
repo root as `Faxanadu (USA).nes` (`*.nes` is gitignored).

```bash
git clone https://github.com/cdmckay/faxanadu-dx
cd faxanadu-dx
./setup.sh
direnv allow
cmake -S . -B build-cycle -DCMAKE_BUILD_TYPE=Release -DNESRECOMP_ROM="$PWD/Faxanadu (USA).nes"
cmake --build build-cycle -j"$(nproc)"
build-cycle/FaxanaduRecomp "Faxanadu (USA).nes"
```

`setup.sh` fetches the code the build depends on. The game opens in its
launcher first. On a Mac it is a program, not an app: start it from Terminal,
as above. Intel Macs aren't supported.

The game's C is in `src/` (each feature is a `fax_<name>.c`), its tests in
`tests/`, the mod in `mods/`, scripts in `tools/` and notes in `docs/`.
`tools/readme_screenshots.py` plays the quest log, the map and Seamless from
your ROM to make the screenshots above.

## Credits

- Built on Matthew Stanley's [FaxanaduRecomp](https://github.com/mstan/FaxanaduRecomp)
  and [nesrecomp](https://github.com/mstan/nesrecomp).
- [chipx86's annotated disassembly](https://github.com/chipx86/faxanadu) of
  Faxanadu, which every feature is built on.
- The Faxanadu 40th Anniversary Edition and Retranslation fan patches, for
  ideas.

## License

PolyForm Noncommercial 1.0.0, with the standing permissions in
[PERMISSIONS.md](PERMISSIONS.md); see [LICENSE](LICENSE). Third-party
components keep their own licenses.
