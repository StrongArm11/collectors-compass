"""Step-by-step "how to get it" text for the pause-map hint panel (mod/data/steps.json, read by gen_mod_items.py).

Gold Skulltulas and Pieces of Heart: built from the tracker's entries, which were already merged from several guides
(the consensus location, what it takes, notes). Big Poes and Silver Rupee rooms: written from guides read for this
(zeldadungeon.net Big Poe guide, finalboss.io Switch 2 Big Poe guide, jegged.com Ice Cavern / Shadow Temple
walkthroughs), keeping what they agree on.
usage: build_steps.py <data dir>"""
import json, os, sys, re
SP = sys.argv[1]
T = json.load(open(os.path.join(SP, 'tracker_data.json')))
C = {c['id']: c for c in json.load(open(os.path.join(SP, 'cats.json')))['cats']}

def when(age, time):
    a = {'Child': ' as child Link', 'Adult': ' as adult Link', 'Child then Adult': ' (first as child, then again as adult)',
         'Child (plant) / Adult (ride)': ' (plant as child, come back as adult)'}.get(age or '', '')
    return a + (' at night' if 'Night' in (time or '') else '')

def tools_step(kind, tools):
    t = [x.strip().replace('|', ' or ') for x in (tools or '').split(';') if x.strip()]
    if not t: return 'Kill it with your sword and pick up the token.' if kind == 'gs' else ''
    s = ', '.join(t)
    if kind == 'gs':
        if 'Magic Bean' in s: return 'Plant a Magic Bean in the soft soil as child; come back as adult and ride the plant to it.'
        if 'bug' in s.lower(): return 'Release bugs from a bottle onto the soft soil; kill the Skulltula that crawls out and grab the token.'
        if any(k in s for k in ('Hookshot', 'Longshot', 'Boomerang')):
            return f'Bring {s}: use it to reach the Skulltula or pull in its token.'
        if any(k in s for k in ('Bombs', 'Hammer')):
            return f'Bring {s}: break open the spot where it hides, then kill it and grab the token.'
        if any(k in s for k in ('Song', 'Lullaby', 'Bolero', 'Requiem')):
            return f'Play {s} to open the way, then kill it and grab the token.'
        return f'Bring {s}, kill the Skulltula and grab the token.'
    return f'Bring {s}.'

def useful(note):
    n = (note or '').strip()
    if not n or re.search(r'guides? (don|do not)|game data|tracker|unsure|unclear|\b(RPG|NI|NL|FB|VGC|GW)\b', n, re.I): return ''
    return n if n.endswith('.') else n + '.'

steps = {}
for r in T['rows']:
    s = [f"Go to {r['area']}{when(r['age'], r['time'])}.", f"Look for it: {r['desc']}."]
    t = tools_step('gs', r['tools'])
    if t: s.append(t)
    n = useful(r.get('notes'))
    if n: s.append(n)
    steps[r['id']] = s
for cat in ('hp',):
    for r in C[cat]['rows']:
        s = [f"Go to {r['area']}{when(r.get('age'), r.get('time'))}.", r['desc'].rstrip('.') + '.']
        t = tools_step('hp', r.get('tools'))
        if t: s.append(t)
        n = useful(r.get('notes'))
        if n: s.append(n)
        steps[r['id']] = s

POE = ['Ride Epona through the spot until the Big Poe appears (on foot you may only get a small Poe).',
       'Shoot it twice with the Fairy Bow before it flies off.',
       'Get off and talk to its soul to catch it in an empty bottle.',
       'Sell it to the Poe Collector, in the old guardhouse at the Castle Town entrance. Ten earn a bottle.']
for r in C['poe']['rows']:
    steps[r['id']] = [f"As adult Link, ride to Hyrule Field: {r['desc'][0].lower() + r['desc'][1:]}."] + POE

SR = {
    'SR-WEL1': ['Go down into the Bottom of the Well as child Link.', 'Take the way into the basement room under the centre of the well.',
                'Use the Lens of Truth: some rupees sit behind fake walls or over hidden holes.',
                'Collect all five silver rupees to open the gate.'],
    'SR-ICE1': ['In the Ice Cavern (adult), go past the spinning-blade room and the Ice Keese.',
                'Melt the red ice with Blue Fire to enter the room with the big ice block.',
                'Push the block across the floor as a step to reach each rupee; push it off an edge to reset it.',
                'All five open the door on the south side.'],
    'SR-ICE2': ['In the Ice Cavern (adult), go past the Freezard room to the room with the spinning ice blade.',
                'Run around the blade: three rupees are in the middle, one behind icicles (break them).',
                'For the last one climb the far platforms and jump down toward the middle.',
                'All five open the far doorway.'],
    'SR-SHA1': ['In the Shadow Temple, go past the Beamos room and the Compass room.',
                'Use the Lens of Truth: two invisible scythes spin around the room - roll under them.',
                'Four rupees are on the floor; Hookshot up to the wooden ledge and jump off for the fifth.',
                'All five open the way to a Small Key.'],
    'SR-SHA2': ['In the Shadow Temple, go to the room with the falling spike blades and a Beamos.',
                'Hover Boots help you cross; ride the rising platform when it comes up.',
                'Collect the four rupees you can see, then bomb the Beamos to free the fifth.',
                'All five open the way on.'],
    'SR-SHA3': ['In the Shadow Temple, go through the Small Key door to the spike room with ReDeads.',
                'Use the Lens of Truth to see the hidden ledges and Hookshot targets.',
                'Hookshot between them to reach all five rupees; keep away from the ReDeads.',
                'All five open the west doorway.'],
    'SR-SPI1': ['In the Spirit Temple (child side), go to the room with the climbing net and the chest.',
                'Collect the rupees on the floor; climb the net for the high ones.',
                'Watch for the rolling boulder and the enemies while you run.', 'All five open the gate to the next room.'],
    'SR-SPI2': ['In the Spirit Temple (child side), go to the room with the sun switch blocks, before the Iron Knuckle.',
                'Push the blocks and use the sun switch to reach the rupees on the high spots.', 'All five open the way on.'],
    'SR-SPI3': ['In the Spirit Temple (adult side), go to the room with the rolling boulders.',
                'Play the Song of Time to move the block and time your runs between the boulders.',
                'Collect all five to open the door.'],
}
for r in C['sr']['rows']:
    steps[r['id']] = SR.get(r['id'], [f"In {r['area']}: {r['desc']}.", 'Collect all five silver rupees in the room to open the way.'])
os.makedirs(os.path.join(SP, 'mod', 'data'), exist_ok=True)
json.dump(steps, open(os.path.join(SP, 'mod', 'data', 'steps.json'), 'w'), indent=1)
print(len(steps), 'items with steps; longest step', max(len(x) for v in steps.values() for x in v))
