/* World V33 routines. Near data pointers address the game's current DS.
 * The entry adapters preserve the original register and flags contract. */
typedef unsigned short Word;

static Word cabinet_read(unsigned port);
#pragma aux cabinet_read = "in ax, dx" parm [dx] value [ax] modify exact [ax]
static void cabinet_write(unsigned port, Word value);
#pragma aux cabinet_write = "out dx, ax" parm [dx] [ax] modify exact []

#ifdef BUILD_RESET
/* 7000:F303..F31D, common reset and diagnostic retry entry. */
void reset_board_outputs(void);
#pragma aux reset_board_outputs modify exact [ax]
void reset_board_outputs(void)
{
    cabinet_write(0x02, 0);  /* coin counters */
    cabinet_write(0x80, 0);  /* playfield one scroll registers */
    cabinet_write(0x84, 0);
    cabinet_write(0x98, 0);  /* playfield master controls */
    cabinet_write(0x9a, 0x10);
    cabinet_write(0x9c, 0x10);
    cabinet_write(0x00, 0);  /* sound command latch */
}
#elif defined(BUILD_OBJECT)
typedef struct Actor {
    Word handler_ip, handler_segment;
    Word next, previous, order;
} Actor;

/* The three spawn paths splice a cleared actor immediately after a parent.
 * The entry shim supplies the caller's segment for that parent. */
Word __far insert_actor(Actor __far *parent, Actor *child);
#pragma aux insert_actor parm [es si] [di] value [si] modify exact [si]
Word __far insert_actor(Actor __far *parent, Actor *child)
{
    Word successor = parent->next;
    Word parent_offset = (Word)(unsigned long)parent;
    child->next = successor;
    parent->next = (Word)child;
    child->previous = parent_offset;
    ((Actor *)successor)->previous = (Word)child;
    child->order = parent->order + 2;
    return successor;
}
#elif defined(BUILD_TORPEDO)
/* 0000:8253..8277. The allocator already cleared the 128-byte actor.
 * Keep the launch offsets in the World ROM so each facing and pose uses
 * the same muzzle position as the original submarine. */
#pragma pack(push, 1)
typedef struct TorpedoPlacement {
    unsigned char before_position[0x21];
    Word x;
    unsigned char y_fraction;
    Word y;
    unsigned char before_facing[3];
    unsigned char facing;
    unsigned char before_pose[0x56 - 0x2a];
    Word pose_offset;
} TorpedoPlacement;
#pragma pack(pop)
Word __far place_torpedo(const TorpedoPlacement __far *submarine, TorpedoPlacement *shot);
#pragma aux place_torpedo parm [es si] [di] value [ax] modify exact [ax]
Word __far place_torpedo(const TorpedoPlacement __far *submarine, TorpedoPlacement *shot)
{
    const Word __far *horizontal = (const Word __far *)0x00009ba9UL;
    shot->facing = submarine->facing;
    shot->x = submarine->x + horizontal[submarine->facing & 1];
    /* The ROM uses a byte offset directly, even for an unaligned word. */
    shot->y = submarine->y + *(const Word __far *)(0x00009b9fUL + submarine->pose_offset);
    return shot->y;
}
#elif defined(BUILD_FIRE)
/* Both standard weapon paths test a cooldown, button edge and projectile
 * allowance in that order. Their object fields have the same spacing. */
Word standard_weapon_ready(volatile Word __far *cooldown, Word fire_mask);
#pragma aux standard_weapon_ready parm [es si] [ax] value [ax] modify exact [ax]
Word standard_weapon_ready(volatile Word __far *cooldown, Word fire_mask)
{
    if (*cooldown) {
        --*cooldown;
        return 0;
    }
    if ((*(volatile Word *)0xb742 & fire_mask) == 0)
        return 0;
    return cooldown[-3];  /* available projectiles, six bytes before cooldown */
}
#elif defined(BUILD_SOUND)
/* 0000:372D..373D. The caller compared the two byte cursors and
 * reaches this block with a pending word in the eight-entry ring. */
Word send_pending_sound(Word read_offset);
#pragma aux send_pending_sound parm [bx] value [ax] modify exact [ax bx]
Word send_pending_sound(Word read_offset)
{
    volatile Word *cursor = (volatile Word *)0xa6d2;
    volatile Word *queue = (volatile Word *)0xa692;
    Word next = (read_offset + 2) & 0x0e;
    Word command;
    *cursor = next;
    command = queue[next / 2];
    cabinet_write(0, command);
    return command;
}
#else
struct InputPair {
    Word held, held_copy;
    Word pressed, pressed_copy;
};

static void latch(volatile struct InputPair *pair, Word buttons)
{
    Word previous = pair->held;
    Word edges = (buttons ^ previous) & buttons;
    pair->held = buttons;
    pair->held_copy = buttons;
    pair->pressed = edges;
    pair->pressed_copy = edges;
}

/* 0000:5E64..5E99. Caller has already selected live or attract input.
 * The other cabinet inputs remain in the board's generic P3/P4 port. */
Word latch_cabinet_inputs(Word player_buttons);
#pragma aux latch_cabinet_inputs parm [ax] value [dx] modify exact [ax dx]
Word latch_cabinet_inputs(Word player_buttons)
{
    volatile struct InputPair *other = (volatile struct InputPair *)0xb744;
    latch((volatile struct InputPair *)0xb73c, player_buttons);
    latch(other, (Word)~cabinet_read(0x06));
    *(volatile Word *)0xb74c = 0;
    return other->pressed;
}
#endif
