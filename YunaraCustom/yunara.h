#include "../plugin_sdk/plugin_sdk.hpp"

namespace yunara
{
#define Q_DRAW_COLOR (MAKE_COLOR(200, 80, 255, 255))
#define W_DRAW_COLOR (MAKE_COLOR(0, 180, 255, 255))
#define E_DRAW_COLOR (MAKE_COLOR(255, 180, 0, 255))
#define R_DRAW_COLOR (MAKE_COLOR(255, 40, 40, 255))

    // Spells
    extern script_spell* q;  // Buff skill, cast without target
    extern script_spell* w;  // Skillshot in both forms
    extern script_spell* e;  // Movement buff or dash (normal)
    extern script_spell* e2; // Dash in Transcendence (R form)
    extern script_spell* r;  // Transcendence toggle

    // Menu
    extern TreeTab* main_tab;

    namespace combo
    {
        extern TreeEntry* use_q;
        extern TreeEntry* use_w; // Use W when enemy is fleeing or crowd-controlled
        extern TreeEntry* use_e;
        extern TreeEntry* use_r;
    }

    namespace harass
    {
        extern TreeEntry* use_q;
        extern TreeEntry* use_w; // Use W for poke/harass
    }

    namespace draw_settings
    {
        extern TreeEntry* draw_q;
        extern TreeEntry* draw_w;
        extern TreeEntry* draw_e;
        extern TreeEntry* draw_r;
    }

    namespace misc
    {
        extern TreeEntry* show_passive;
        extern TreeEntry* debug_output;
    }

    // Passive logic
    extern int passive_stacks;
    extern float last_stack_time;
    extern bool r_active;

    // State & logic
    void load();
    void unload();

    void on_update();
    void on_draw();
    void on_create(game_object_script obj);
    void on_before_attack(game_object_script target, bool* process);
    void on_attack(game_object_script target);

    void q_logic(); // Cast if 8 stacks OR auto-activate in R (resets stacks)
    void w_logic(); // Use to slow fleeing enemies or poke CC'd ones
    void e_logic(); // Use for escape/chase (switches to e2 under R)
    void r_logic(); // Enter/exit transcendence
    void handle_passive();
    void enter_transcendence();
    void exit_transcendence();

    // State helpers
    inline bool is_transcendent() { return r_active && myhero->has_buff(buff_hash("YunaraR")); }
    inline bool has_cc(game_object_script target) {
        return target->has_buff_type(buff_type::Slow) ||
            target->has_buff_type(buff_type::Snare) ||
            target->has_buff_type(buff_type::Stun) ||
            target->has_buff_type(buff_type::Suppression) ||
            target->has_buff_type(buff_type::Taunt) ||
            target->has_buff_type(buff_type::Charm) ||
            target->has_buff_type(buff_type::Fear) ||
            target->has_buff_type(buff_type::Knockup);
    }
}
