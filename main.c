#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <SDL.h>

#define GENE_COUNT 16
#define CELL_SCARCITY 48
#define FOOD_SCARCITY 11
#define CLUMP_COUNT 30
#define MUTATION_RATE 0.03
#define CELL_START_SCORE 250
#define FOOD_SCORE 250
#define CELL_MITOSIS_THRESHOLD 1000
#define FRAME_INTERVAL 0

typedef enum {
    FOOD_SPAWN_CLUMP,
    FOOD_SPAWN_RANDOM,
} FoodSpawn;

FoodSpawn food_spawn = FOOD_SPAWN_CLUMP;

typedef enum {
    FOOD_REBIRTH_SOMEWHERE,
    FOOD_REBIRTH_NEARBY,
} FoodRebirth;

FoodRebirth food_rebirth = FOOD_REBIRTH_SOMEWHERE;

typedef enum {
    ACTION_MOVE_FORWARD,
    ACTION_TURN_LEFT,
    ACTION_TURN_RIGHT,
    ACTION_MOVE_BACKWARD,
    ACTION_MAX,
} Action;

static int const action_costs[ACTION_MAX] = {8, 3, 3, 5};

typedef enum {
    SITUATION_EMPTY,
    SITUATION_FOOD,
    SITUATION_LIFE,
    SITUATION_WALL,
    SITUATION_MAX,
} Situation;

// ordered so that +1 is a right turn and -1 is a left turn
typedef enum {
    FACING_NORTH,
    FACING_EAST,
    FACING_SOUTH,
    FACING_WEST,
    FACING_MAX,
} Facing;

typedef struct {
    Action action;
    int next_state;
} Response;

typedef struct {
    Response responses[SITUATION_MAX];
} Gene;

typedef struct {
    Gene genes[GENE_COUNT];
} Chromosome;

typedef enum {
    ENTITY_TYPE_NONE,
    ENTITY_TYPE_CELL,
    ENTITY_TYPE_FOOD,
} EntityType;

typedef struct {
    EntityType type;
    int last_update_color;
} Entity;

typedef struct {
    int x;
    int y;
} Coord;

typedef struct {
    Entity entity;
    int state;
    int score;
    Chromosome chromosome;
    Facing facing;
    uint32_t color;
} Cell;

typedef struct {
    Entity entity;
    int clump;
} Food;

typedef struct {
    Coord coord;
} Clump;

typedef struct {
    int width;
    int height;
    Clump clumps[CLUMP_COUNT];
    Entity *entities[0];
} World;

bool coord_in_bounds(Coord coord, World const *world)
{
    return coord.x >= 0 && coord.x < world->width
        && coord.y >= 0 && coord.y < world->height;
}

static Entity **world_get_entity_ref(World *world, Coord coord)
{
    if (coord.x >= 0 && coord.x < world->width
        && coord.y >= 0 && coord.y < world->height)
    {
        return &world->entities[coord.y * world->width + coord.x];
    }
    return NULL;
}

Facing facing_random()
{
    static int facing;
    return ++facing % FACING_MAX;
}

Chromosome chromosome_random()
{
    Chromosome ret;
    memset(&ret, -1, sizeof(ret));
    for (size_t i = 0; i < GENE_COUNT; ++i) {
        for (size_t j = 0; j < SITUATION_MAX; ++j) {
            ret.genes[i].responses[j] = (Response) {
                .action = lrand48() % ACTION_MAX,
                .next_state = lrand48() % GENE_COUNT,
            };
        }
    }
    return ret;
}

Facing facing_turn(Facing facing, int turn)
{
    //assert((-1 % FACING_MAX) == FACING_WEST);
    if (turn < 0)
        turn *= -3;
    return (facing + turn) % FACING_MAX;
}

void chromosome_mutate(Chromosome *c)
{
    for (size_t state = 0; state < GENE_COUNT; ++state) {
        for (size_t situation = 0; situation < SITUATION_MAX; ++situation) {
            Response *response = &c->genes[state].responses[situation];
            if (drand48() < MUTATION_RATE)
                response->action = lrand48() % ACTION_MAX;
            if (drand48() < MUTATION_RATE)
                response->next_state = lrand48() % GENE_COUNT;
        }
    }
}

// could return SDL_Color?
uint32_t cell_get_color(Cell const *cell)
{
    uint32_t value = 0;
    for (size_t gene = 0; gene < GENE_COUNT; ++gene) {
        uint32_t series = 0;
        for (size_t situation = 0; situation < SITUATION_MAX; ++situation) {
            Response const *response = &cell->chromosome.genes[gene].responses[situation];
            series = series << 6;
            series |= response->action * 16 + response->next_state % 16;
        }
        assert(series < (1 << 24));
        value = value ^ series;
    }
    return value;
}

Chromosome chromosome_big_square(void)
{
    Chromosome c;
    for (size_t gene = 0; gene < 15; ++gene) {
        c.genes[gene] = (Gene) {
            .responses = {
                [SITUATION_EMPTY ... SITUATION_FOOD] = {ACTION_MOVE_FORWARD, gene + 1},
                [SITUATION_LIFE ... SITUATION_WALL] = {ACTION_TURN_LEFT, 0},
            }
        };
    }
    c.genes[15] = (Gene){.responses = {{ACTION_TURN_LEFT, 0}}};
    c.genes[15].responses[SITUATION_FOOD] = (Response){ACTION_MOVE_FORWARD, 15};
    return c;
}

Cell *cell_new(Cell *parent)
{
    Cell *cell = malloc(sizeof(Cell));
    *cell = (Cell) {
        .entity = (Entity) {
            .type = ENTITY_TYPE_CELL,
            .last_update_color = -1},
    };
    if (parent) {
        cell->entity.last_update_color = parent->entity.last_update_color;
        cell->chromosome = parent->chromosome;
        chromosome_mutate(&cell->chromosome);
        cell->facing = facing_turn(parent->facing, 2);
        cell->score = CELL_START_SCORE;
    } else {
        cell->chromosome = /*chromosome_random()*/chromosome_big_square();
        chromosome_mutate(&cell->chromosome);
        cell->facing = facing_random();
        cell->score = CELL_START_SCORE;
    }
    cell->color = cell_get_color(cell);
    return cell;
}

Food *food_new(void)
{
    Food *food = malloc(sizeof(Food));
    *food = (Food) {
        .entity = {ENTITY_TYPE_FOOD},
        .clump = -1,
    };
    return food;
}

int random_int(int min, int max)
{
    unsigned int range = max - min;
    long value = mrand48() % range;
    if (value < 0) value += range;
    assert(0 <= value && value < range);
    value += min;
    assert(value >= INT_MIN && value <= INT_MAX);
    return value;
}

Coord perturb_coord(Coord coord)
{
    coord.x += random_int(-1, 2);
    coord.y += random_int(-1, 2);
    return coord;
}

Coord find_nearby_empty(Coord coord, World *world)
{
    assert(coord_in_bounds(coord, world));
    while (true) {
        Coord new_coord = perturb_coord(coord);
        Entity **ent_ref = world_get_entity_ref(world, new_coord);
        if (ent_ref) {
            coord = new_coord;
            if (!*ent_ref) return coord;
        }
    }
}

World *world_new(int width, int height)
{
    World *world = malloc(sizeof(World) + width * height * sizeof(void *));
    *world = (World) {
        .width = width,
        .height = height,
    };
    for (size_t i = 0; i < world->width * world->height; ++i) {
        Entity *entity = NULL;
        if (!(i % CELL_SCARCITY)) {
            entity = (Entity *)cell_new(NULL);
        }
        world->entities[i] = entity;
    }
    size_t ai_index = (world->height / 2) * world->width + (world->width / 2);
    free(world->entities[ai_index]);
    Cell *ai_cell = cell_new(NULL);
    ai_cell->chromosome = chromosome_big_square();
    ai_cell->facing = FACING_NORTH;
    world->entities[ai_index] = (Entity *)ai_cell;

    for (size_t i = 0; i < CLUMP_COUNT; ++i) {
        world->clumps[i].coord = (Coord){.x = random_int(0, world->width), .y = random_int(0, world->height)};
    }
    for (size_t i = 0; i < (world->width * world->height) / FOOD_SCARCITY; ++i) {
        int clump = i % CLUMP_COUNT;
        Food *food = food_new();
        food->clump = clump;
        Coord coord = find_nearby_empty(world->clumps[clump].coord, world);
        Entity **ent_ref = world_get_entity_ref(world, coord);
        *ent_ref = (Entity *)food;
    }

    return world;
}

void draw_screen(SDL_Surface *screen, World const *world)
{
    Uint16 cell_size = 8;
    //Uint32 cell_color = SDL_MapRGB(screen->format, -1, -1, 0);
    Uint32 const food_color = SDL_MapRGB(screen->format, 0, -1, 0);
    Uint32 const no_color = SDL_MapRGB(screen->format, 0, 0, 0);
    for (size_t y = 0; y < world->height; ++y) {
        for (size_t x = 0; x < world->width; ++x) {
            Uint32 color = no_color;
            Entity *entity = world->entities[y * world->width + x];
            if (entity) {
                switch (entity->type) {
                case ENTITY_TYPE_FOOD:
                    color = food_color;
                    break;
                case ENTITY_TYPE_CELL:
                    color = ((Cell *)entity)->color;
                    break;
                default:
                    abort();
                }
            }
            SDL_FillRect(
                screen,
                &(SDL_Rect){
                    .x = x * cell_size,
                    .y = y * cell_size,
                    .w = cell_size,
                    .h = cell_size},
                color);
        }
    }
}

Coord facing_step(Facing const facing, Coord coord, int const steps)
{
    switch (facing) {
    case FACING_NORTH:
        coord.y -= steps;
        break;
    case FACING_SOUTH:
        coord.y += steps;
        break;
    case FACING_WEST:
        coord.x -= steps;
        break;
    case FACING_EAST:
        coord.x += steps;
        break;
    default:
        abort();
    }
    return coord;
}

bool coord_equal(Coord *self, Coord *other)
{
    return self->x == other->x && self->y == other->y;
}

void relocate_food(World *world, Coord coord)
{
    Entity **food_ref = world_get_entity_ref(world, coord);
    assert((*food_ref)->type == ENTITY_TYPE_FOOD);
    switch (food_spawn) {
    case FOOD_SPAWN_RANDOM:
        switch (food_rebirth) {
        case FOOD_REBIRTH_NEARBY:
            break;
        case FOOD_REBIRTH_SOMEWHERE:
            coord.x = random_int(0, world->width);
            coord.y = random_int(0, world->height);
            break;
        default:
            abort();
        }
        break;
    case FOOD_SPAWN_CLUMP:
        {
            int clump = ((Food *)*food_ref)->clump;
            if (drand48() < 0.05) {
                Coord *clump_coord = &world->clumps[clump].coord;
                clump_coord->x = random_int(0, world->width);
                clump_coord->y = random_int(0, world->height);
            }
            switch (food_rebirth) {
            case FOOD_REBIRTH_NEARBY:
                coord = world->clumps[clump].coord;
                break;
            case FOOD_REBIRTH_SOMEWHERE:
                coord = world->clumps[random_int(0, CLUMP_COUNT)].coord;
                break;
            default:
                abort();
            }
        }
    }
    while (true) {
        coord = find_nearby_empty(coord, world);
        Entity **new_ref = world_get_entity_ref(world, coord);
        assert(!*new_ref);
        *new_ref = (Entity *)*food_ref;
        *food_ref = NULL;
        return;
    }
    assert(false);
    // we weren't able to place the food, so destroy it in its current location
    free(*food_ref);
    *food_ref = NULL;
}

void entity_update(
    World *const world,
    Coord const start_pos,
    int const update_color)
{
    Entity **entity = &world->entities[start_pos.y * world->width + start_pos.x];
    if (!*entity)
        return;
    if ((*entity)->last_update_color == update_color)
        return;
    else
        (*entity)->last_update_color = update_color;
    if ((*entity)->type != ENTITY_TYPE_CELL)
        return;
    Cell *cell = (Cell *)*entity;
    Situation situation = SITUATION_EMPTY;
    Coord const faced_coord = facing_step(cell->facing, start_pos, 1);
    Entity **faced_entity = NULL;
    if (    faced_coord.x < 0 || faced_coord.x >= world->width
            || faced_coord.y < 0 || faced_coord.y >= world->height) {
        situation = SITUATION_WALL;
    } else {
        faced_entity = world_get_entity_ref(world, faced_coord);
        if (*faced_entity) {
            switch ((*faced_entity)->type) {
            case ENTITY_TYPE_CELL:
                situation = SITUATION_LIFE;
                break;
            case ENTITY_TYPE_FOOD:
                situation = SITUATION_FOOD;
                break;
            default:
                abort();
            }
        } else {
            situation = SITUATION_EMPTY;
        }
    }
    assert(cell->state >= 0 && cell->state < GENE_COUNT);
    assert(situation >= 0 && situation < SITUATION_MAX);
    Response response = cell->chromosome.genes[cell->state].responses[situation];
    switch (response.action) {
    case ACTION_TURN_LEFT:
        cell->facing = facing_turn(cell->facing, -1);
        break;
    case ACTION_TURN_RIGHT:
        cell->facing = facing_turn(cell->facing, 1);
        break;
    case ACTION_MOVE_FORWARD:
    case ACTION_MOVE_BACKWARD:
        {
            Coord dest_coord = facing_step(
                cell->facing,
                start_pos,
                (ACTION_MOVE_FORWARD ? 1 : -1));
            if (!coord_in_bounds(dest_coord, world))
                break;
            Entity **dest_ent_ptr = &world->entities[dest_coord.y * world->width + dest_coord.x];
            if (*dest_ent_ptr) {
                if ((*dest_ent_ptr)->type == ENTITY_TYPE_FOOD) {
                    cell->score += FOOD_SCORE;
                    relocate_food(world, dest_coord);
                } else {
                    break;
                }
            }
            assert(!*dest_ent_ptr);
            *dest_ent_ptr = (Entity *)cell;
            *entity = NULL;
            if (cell->score >= CELL_MITOSIS_THRESHOLD) {
                *entity = (Entity *)cell_new(cell);
                cell->score = CELL_START_SCORE;
            }
            entity = dest_ent_ptr;
        }
        break;
    default:
        abort();
    }
    cell->score -= action_costs[response.action];
    cell->state = response.next_state;
    //assert(cell->score >= 0);
    if (cell->score <= 0) {
        free(*entity);
        *entity = NULL;
    }
}

void update_world(World *world, int turn_color)
{
    for (size_t y = 0; y < world->height; ++y) {
        for (size_t x = 0; x < world->width; ++x) {
            entity_update(world, (Coord){.x = x, .y = y}, turn_color);
        }
    }
}

int main(int argc, char **argv)
{
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Surface *screen = SDL_SetVideoMode(640, 480, 0, 0);
    srand48(time(NULL));
    World *world = world_new(80, 60);
    Uint32 last_ticks = SDL_GetTicks();
    int turn_color = 0;
    bool quit = false;
    while (!quit) {
        turn_color = !turn_color;
        draw_screen(screen, world);
        SDL_Flip(screen);
        for (SDL_Event event; SDL_PollEvent(&event);) {
            if (event.type == SDL_QUIT) {
                quit = true;
                break;
            }
        }
        update_world(world, turn_color);
        Uint32 ticks = SDL_GetTicks();
        Uint32 next_ticks = last_ticks + FRAME_INTERVAL;
        if (ticks <= next_ticks)
            SDL_Delay(next_ticks - ticks);
        else
            next_ticks = ticks;
        last_ticks = next_ticks;
    }
    SDL_Quit();
    return 0;
}
