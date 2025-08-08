#include "yunara.h"
#include "math_helpers.h"
#include "../plugin_sdk/plugin_sdk.hpp"
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace yunara
{
    // ==== CONSTS ====
    constexpr float Q_AOE_RADIUS = 260.0f;
    constexpr float Q_STACK_DURATION = 6.0f;
    constexpr float Q_DECAY_TICK = 0.5f;
    constexpr float POST_E_W_DELAY = 0.13f;
    constexpr float GAPCLOSER_RADIUS = 425.0f; // радиус триггера анти-gapcloser
    constexpr float GAPCLOSER_CHECK_RANGE = 900.0f;

    // ==== SPELLS ====
    static script_spell* q = nullptr;
    static script_spell* w = nullptr;
    static script_spell* e = nullptr;
    static script_spell* r = nullptr;

    // ==== STATE ====
    static int unleash_stacks = 0;
    static float last_stack_time = 0.0f;
    static float stacks_start_decay = 0.0f;
    static float last_decay_tick = 0.0f;
    static bool is_unleashed = false;
    static float unleashed_end_time = 0.0f;
    static bool want_post_e_w = false;
    static float want_post_e_w_until = 0.0f;

    static bool allow_e_tower = false;
    static bool prev_tower_hotkey = false;

    // Gapcloser tracking
    static std::unordered_map<uint32_t, bool> prev_dashing;

    // ==== SETTINGS ====
    namespace settings
    {
        TreeTab* main_tab = nullptr;
        TreeEntry* farm_q = nullptr, * farm_hotkey = nullptr, * q_aoe_farm_mode = nullptr, * q_use_if_expiring_farm = nullptr,
            * q_expiring_radius_farm = nullptr, * auto_q = nullptr, * q_use_if_expiring_combo = nullptr, * q_expiring_radius_combo = nullptr,
            * auto_w = nullptr, * w_auto_control = nullptr, * w_auto_predict = nullptr, * w_farm_min_targets = nullptr,
            * auto_e = nullptr, * e_safe_distance = nullptr, * e_jump_mode = nullptr, * e_my_hp_check = nullptr, * e_my_hp = nullptr,
            * e_enemy_hp_check = nullptr, * e_enemy_hp = nullptr, * e_antimelee_enable = nullptr, * e_antimelee_range = nullptr,
            * auto_r = nullptr, * r_min_hp = nullptr, * r_min_enemies = nullptr, * r_min_enemy_hp = nullptr, * r_skip_if_q_buff = nullptr,
            * draw_range_w = nullptr, * draw_range_e = nullptr, * auto_ks_w_ew = nullptr, * allow_tower = nullptr,
            * enable_antigap = nullptr, * enable_interrupt = nullptr;
    }

    // ==== HELPERS ====
    inline bool is_valid_object(const game_object_script& obj)
    {
        return obj && obj->is_valid() && !obj->is_dead();
    }
    bool has_buff_icase(const game_object_script& obj, const std::string& name)
    {
        for (const auto& buff : obj->get_bufflist())
            if (buff && buff->is_valid() && _stricmp(buff->get_name().c_str(), name.c_str()) == 0)
                return true;
        return false;
    }

    bool is_r_active()
    {
        return has_buff_icase(myhero, "YunaraR");
    }

    // ==== ANTI-GAPCLOSER LOGIC ====
    void check_gapcloser()
    {
        if (!settings::enable_antigap || !settings::enable_antigap->get_bool()) return;

        for (auto& enemy : entitylist->get_enemy_heroes())
        {
            if (!enemy || !enemy->is_valid_target(GAPCLOSER_CHECK_RANGE)) continue;

            bool is_dash = enemy->is_dashing();
            uint32_t net_id = enemy->get_network_id();

            // Только на старт нового даша
            if (is_dash && !prev_dashing[net_id])
            {
                if (enemy->get_distance(myhero) < GAPCLOSER_RADIUS && e && e->is_ready())
                {
                    vector from_enemy = myhero->get_position() - enemy->get_position();
                    normalize_vector(from_enemy);
                    float dash_range = settings::e_safe_distance ? (float)settings::e_safe_distance->get_int() : 350.f;
                    vector dash_pos = myhero->get_position() + from_enemy * dash_range;

                    bool can_dash = true;
                    if (!allow_e_tower)
                    {
                        for (auto& turret : entitylist->get_enemy_turrets())
                            if (turret->get_distance(dash_pos) < 875.f)
                                can_dash = false;
                    }
                    if (can_dash)
                        e->cast(dash_pos);
                }
            }
            prev_dashing[net_id] = is_dash;
        }
    }

    // ==== INTERRUPTIBLE SPELLS LOGIC ====
    void check_interruptible()
    {
        if (!settings::enable_interrupt || !settings::enable_interrupt->get_bool()) return;

        std::vector<std::string> interrupt_spells = {
            "KatarinaR", "FiddleSticksW", "JannaR", "VelkozR", "MalzaharR", "GalioR"
        };

        for (auto& enemy : entitylist->get_enemy_heroes())
        {
            if (!enemy || !enemy->is_valid_target(w->range())) continue;

            auto spell = enemy->get_active_spell();
            if (spell && spell->is_channeling())
            {
                auto spell_data = spell->get_spell_data();
                if (spell_data)
                {
                    std::string name = spell_data->get_name();
                    if (std::find(interrupt_spells.begin(), interrupt_spells.end(), name) != interrupt_spells.end())
                    {
                        if (w && w->is_ready())
                        {
                            auto pred = w->get_prediction(enemy);
                            if (pred.hitchance >= hit_chance::high)
                                w->cast(pred.get_cast_position());
                        }
                    }
                }
            }
        }
    }


    // ==== Q LOGIC ====
    void after_q_cast()
    {
        is_unleashed = true;
        unleash_stacks = 0;
        unleashed_end_time = gametime->get_time() + Q_STACK_DURATION;
    }

    void on_after_attack(game_object_script target)
    {
        if (!is_valid_object(target) || myhero->is_dead())
            return;

        unleash_stacks += target->is_ai_hero() ? 2 : 1;
        unleash_stacks = std::min(unleash_stacks, 8);
        last_stack_time = gametime->get_time();

        if (q && q->is_ready() && (target->is_ai_hero() || target->is_ai_turret() || target->is_epic_monster() || target->is_inhibitor() || target->is_nexus()))
        {
            q->cast();
            orbwalker->reset_auto_attack_timer();
            after_q_cast();
        }
    }

    void farm_with_q()
    {
        if (!settings::farm_q->get_bool() || !q->is_ready() || unleash_stacks < 8) return;
        if (settings::farm_hotkey && !settings::farm_hotkey->get_bool()) return;

        auto minions = entitylist->get_enemy_minions();
        auto jungle = entitylist->get_jugnle_mobs_minions();
        minions.insert(minions.end(), jungle.begin(), jungle.end());
        auto turrets = entitylist->get_enemy_turrets();
        minions.insert(minions.end(), turrets.begin(), turrets.end());

        if (settings::q_use_if_expiring_farm->get_bool() && unleash_stacks >= 8 && !is_unleashed) {
            float now = gametime->get_time();
            if ((last_stack_time + Q_STACK_DURATION - now) < 0.7f) {
                for (auto& m : minions)
                    if (is_valid_object(m) && m->is_valid_target(settings::q_expiring_radius_farm->get_int()) && q->cast()) {
                        after_q_cast(); break;
                    }
            }
        }
        if (settings::q_aoe_farm_mode->get_bool())
        {
            int minion_threshold = 2;
            for (auto& main : minions) {
                if (!is_valid_object(main) || !main->is_valid_target(q->range())) continue;
                int aoe_count = 0;
                vector hero_to_main = main->get_position() - myhero->get_position();
                for (auto& other : minions) {
                    if (other == main || !is_valid_object(other) || !other->is_valid_target(q->range())) continue;
                    vector main_to_other = other->get_position() - main->get_position();
                    float forward = (hero_to_main.x * main_to_other.x + hero_to_main.y * main_to_other.y);
                    if (forward <= 0) continue;
                    float dist = sqrtf(main_to_other.x * main_to_other.x + main_to_other.y * main_to_other.y);
                    if (dist < Q_AOE_RADIUS) aoe_count++;
                }
                if (aoe_count >= minion_threshold)
                    if (q->cast()) { after_q_cast(); break; }
            }
        }
        else
        {
            for (auto& m : minions)
                if (is_valid_object(m) && m->is_valid_target(q->range()) && q->cast()) { after_q_cast(); break; }
        }
    }

    void try_cast_q()
    {
        if (is_unleashed || !settings::auto_q->get_bool() || !q->is_ready() || unleash_stacks < 8) return;
        for (auto& enemy : entitylist->get_enemy_heroes())
        {
            if (is_valid_object(enemy) &&
                enemy->get_distance(myhero) <= myhero->get_attack_range() + myhero->get_bounding_radius() + 50.0f)
            {
                if (q->cast()) { after_q_cast(); break; }
            }
        }
        if (settings::q_use_if_expiring_combo->get_bool() && unleash_stacks >= 8 && !is_unleashed)
        {
            float now = gametime->get_time();
            if ((last_stack_time + Q_STACK_DURATION - now) < 0.5f) {
                for (auto& enemy : entitylist->get_enemy_heroes()) {
                    if (is_valid_object(enemy) &&
                        enemy->get_distance(myhero) <= settings::q_expiring_radius_combo->get_int() && enemy->is_visible()) {
                        if (q->cast()) { after_q_cast(); break; }
                    }
                }
            }
        }
    }

    // ==== W LOGIC (улучшенный предикт) ====
    void try_cast_w()
    {
        if (!settings::auto_w->get_bool() || !w->is_ready()) return;

        if (settings::w_auto_predict->get_bool()) {
            auto target = target_selector->get_target(w->range(), damage_type::magical);

            if (is_valid_object(target)) {
                auto pred = w->get_prediction(target);

                // Можно адаптировать предикт под стадию игры, учитывать окружение, скорость цели и т.д.
                if (pred.hitchance >= hit_chance::low ||
                    (is_r_active() && pred.hitchance >= hit_chance::medium))
                {
                    w->cast(pred.get_cast_position());
                }
            }
        }
    }

    void try_ks_w_ew()
    {
        if (!settings::auto_ks_w_ew->get_bool()) return;
        if (!w->is_ready()) return;

        int lw = w->level();
        if (lw == 0) return;
        float base_w[] = { 0, 80, 130, 180, 230, 280 };
        float raw_w = base_w[lw] + 0.7f * myhero->get_total_ability_power();

        for (auto& enemy : entitylist->get_enemy_heroes())
        {
            if (!is_valid_object(enemy) || !enemy->is_valid_target(w->range())) continue;
            if (!settings::allow_tower->get_bool() && enemy->is_under_enemy_turret()) continue;

            float dealt_w = plugin_sdk->get_damagelib_manager()->calculate_damage_on_unit(myhero, enemy, damage_type::magical, raw_w);
            if (enemy->get_health() <= dealt_w)
            {
                auto pred = w->get_prediction(enemy);
                if (pred.hitchance >= hit_chance::high)
                    w->cast(pred.get_cast_position());
                continue;
            }
            if (is_r_active())
            {
                float bonus_raw = 0.15f * enemy->get_max_health();
                float dealt_bonus = plugin_sdk->get_damagelib_manager()->calculate_damage_on_unit(myhero, enemy, damage_type::magical, bonus_raw);
                if (enemy->get_health() <= dealt_w + dealt_bonus)
                {
                    auto pred = w->get_prediction(enemy);
                    if (pred.hitchance >= hit_chance::high)
                        w->cast(pred.get_cast_position());
                }
            }
        }
    }

    void try_cast_w_to_cc()
    {
        if (!settings::w_auto_control->get_bool() || !w || !w->is_ready()) {
            for (auto& enemy : entitylist->get_enemy_heroes()) {
                if (!settings::allow_tower->get_bool() && enemy->is_under_enemy_turret()) continue;
                if (!is_valid_object(enemy) || !enemy->is_valid_target(w->range())) continue;
                if (enemy->has_buff_type(buff_type::Stun) || enemy->has_buff_type(buff_type::Snare) ||
                    enemy->has_buff_type(buff_type::Fear) || enemy->has_buff_type(buff_type::Slow)) {
                    auto pred = w->get_prediction(enemy);
                    if (pred.hitchance >= hit_chance::low)
                        w->cast(pred.get_cast_position());
                    return;
                }
            }
        }
    }

    void farm_with_w()
    {
        if (!settings::auto_w->get_bool() || !w || !w->is_ready()) return;
        if (settings::farm_hotkey && !settings::farm_hotkey->get_bool()) return;
        if (!(orbwalker->lane_clear_mode() || orbwalker->last_hit_mode())) return;

        auto minions = entitylist->get_enemy_minions();
        auto jungle = entitylist->get_jugnle_mobs_minions();
        minions.insert(minions.end(), jungle.begin(), jungle.end());

        int best_hits = 0;
        vector best_pos = { 0,0,0 };
        for (auto& main : minions)
        {
            if (!is_valid_object(main) || !main->is_valid_target(w->range())) continue;
            if (!settings::allow_tower->get_bool() && main->is_under_enemy_turret()) continue;
            int count = 1;
            vector pos = main->get_position();
            for (auto& other : minions)
            {
                if (other == main || !is_valid_object(other) || !other->is_valid_target(w->range())) continue;
                vector to_other = other->get_position() - pos;
                if (to_other.length() < 100.0f) count++;
            }
            if (count > best_hits)
            {
                best_hits = count;
                best_pos = pos;
            }
        }
        int min_targets = settings::w_farm_min_targets
            ? settings::w_farm_min_targets->get_int()
            : 2;

        if (best_hits >= min_targets)
            w->cast(best_pos);
    }

    // ==== E LOGIC ====
    enum class e_mode_t { ToCursor, Side, Away, AntiMelee };

    void try_cast_e()
    {
        int mode = settings::e_jump_mode ? settings::e_jump_mode->get_int() : 0;
        float dash_range = settings::e_safe_distance ? (float)settings::e_safe_distance->get_int() : 350.0f;
        vector hero_pos = myhero->get_position(), dash_pos = hero_pos;

        if (settings::e_antimelee_enable && settings::e_antimelee_enable->get_bool())
        {
            float melee_range = settings::e_antimelee_range ? (float)settings::e_antimelee_range->get_int() : 300.f;
            game_object_script melee_enemy = nullptr;
            float min_melee_dist = FLT_MAX;
            for (auto& enemy : entitylist->get_enemy_heroes())
            {
                if (!enemy || !enemy->is_valid_target(700.0f)) continue;
                if (!enemy->is_melee()) continue;
                float d = myhero->get_distance(enemy);
                if (d < melee_range && d < min_melee_dist)
                {
                    min_melee_dist = d;
                    melee_enemy = enemy;
                }
            }
            if (melee_enemy)
            {
                vector from_melee = myhero->get_position() - melee_enemy->get_position();
                normalize_vector(from_melee);
                float dash_range = settings::e_safe_distance ? (float)settings::e_safe_distance->get_int() : 350.0f;
                vector dash_pos = myhero->get_position() + from_melee * dash_range;

                bool can_dash = true;
                if (!allow_e_tower)
                {
                    for (auto& turret : entitylist->get_enemy_turrets())
                        if (turret->get_distance(dash_pos) < 875.f)
                            can_dash = false;
                }
                if (can_dash && e->cast(dash_pos) && is_r_active())
                {
                    want_post_e_w = true;
                    want_post_e_w_until = gametime->get_time() + POST_E_W_DELAY;
                }
                return;
            }
        }

        if (!settings::auto_e || !settings::auto_e->get_bool() || !e || !e->is_ready() || myhero->is_dead())
            return;
        if (!game_input || !renderer) return;

        // Dash logic
        if (mode == 0)
        {
            point2 mouse = game_input->get_game_cursor_pos();
            vector screen_mouse((float)mouse.x, (float)mouse.y, 0.0f);
            vector cursor_world;
            renderer->screen_to_world(screen_mouse, cursor_world);

            vector dir = cursor_world - hero_pos;
            normalize_vector(dir);
            dash_pos = hero_pos + dir * dash_range;
        }
        else if (mode == 1)
        {
            game_object_script closest = nullptr;
            float min_dist = FLT_MAX;
            for (auto& enemy : entitylist->get_enemy_heroes())
            {
                if (!is_valid_object(enemy) || !enemy->is_valid_target(700.0f)) continue;
                float d = myhero->get_distance(enemy);
                if (d < min_dist) { min_dist = d; closest = enemy; }
            }
            if (closest)
            {
                vector enemy_dir = hero_pos - closest->get_position();
                normalize_vector(enemy_dir);

                vector perp1(-enemy_dir.y, enemy_dir.x, 0.f);
                vector perp2(enemy_dir.y, -enemy_dir.x, 0.f);

                point2 mouse = game_input->get_game_cursor_pos();
                vector screen_mouse((float)mouse.x, (float)mouse.y, 0.0f);
                vector cursor_world;
                renderer->screen_to_world(screen_mouse, cursor_world);

                vector dash_pos1 = hero_pos + perp1 * dash_range;
                vector dash_pos2 = hero_pos + perp2 * dash_range;

                float dist1 = dash_pos1.distance(cursor_world);
                float dist2 = dash_pos2.distance(cursor_world);
                dash_pos = (dist1 < dist2) ? dash_pos1 : dash_pos2;
            }
            else return;
        }
        else if (mode == 2)
        {
            game_object_script closest = nullptr;
            float min_dist = FLT_MAX;
            for (auto& enemy : entitylist->get_enemy_heroes())
            {
                if (!is_valid_object(enemy) || !enemy->is_valid_target(700.0f)) continue;
                float d = myhero->get_distance(enemy);
                if (d < min_dist) { min_dist = d; closest = enemy; }
            }
            if (closest)
            {
                vector from_enemy = hero_pos - closest->get_position();
                normalize_vector(from_enemy);
                dash_pos = hero_pos + from_enemy * dash_range;
            }
            else return;
        }
        else if (mode == 3)
        {
            game_object_script closest = nullptr;
            float min_dist = FLT_MAX;
            for (auto& enemy : entitylist->get_enemy_heroes())
            {
                if (!is_valid_object(enemy) || !enemy->is_valid_target(700.0f)) continue;
                if (!enemy->is_melee()) continue;
                float d = myhero->get_distance(enemy);
                if (d < min_dist) { min_dist = d; closest = enemy; }
            }
            if (closest && min_dist < 350.f)
            {
                vector from_enemy = hero_pos - closest->get_position();
                normalize_vector(from_enemy);
                dash_pos = hero_pos + from_enemy * dash_range;
            }
            else return;
        }
        else return;

        if (!allow_e_tower)
        {
            for (auto& turret : entitylist->get_enemy_turrets())
                if (turret->get_distance(dash_pos) < 875.0f)
                    return;
        }

        if (e->cast(dash_pos) && is_r_active())
        {
            want_post_e_w = true;
            want_post_e_w_until = gametime->get_time() + POST_E_W_DELAY;
        }
    }

    void e_flee()
    {
        if (!e || !e->is_ready() || !myhero || myhero->is_dead()) return;
        if (!game_input || !renderer) return;

        point2 mouse = game_input->get_game_cursor_pos();
        vector screen_mouse((float)mouse.x, (float)mouse.y, 0.0f);
        vector cursor_world;
        renderer->screen_to_world(screen_mouse, cursor_world);

        vector hero_pos = myhero->get_position();
        vector dir = cursor_world - hero_pos;
        normalize_vector(dir);

        float dash_range = settings::e_safe_distance ? (float)settings::e_safe_distance->get_int() : 400.0f;
        vector dash_pos = hero_pos + dir * dash_range;

        if (!allow_e_tower)
        {
            for (auto& turret : entitylist->get_enemy_turrets())
                if (turret->get_distance(dash_pos) < 875.0f)
                    return;
        }

        e->cast(dash_pos);
    }

    // ==== R LOGIC ====
    void try_cast_r()
    {
        if (!settings::auto_r->get_bool() || !r->is_ready()) return;
        if (settings::r_skip_if_q_buff->get_bool() && is_unleashed) return;
        if (myhero->get_health_percent() < settings::r_min_hp->get_int()) return;

        int enemy_count = 0;
        for (auto& hero : entitylist->get_enemy_heroes())
        {
            if (is_valid_object(hero) && hero->get_distance(myhero) < 700.0f &&
                hero->get_health_percent() < settings::r_min_enemy_hp->get_int())
                enemy_count++;
        }
        if (enemy_count >= settings::r_min_enemies->get_int())
            r->cast();
    }

    // ==== MAIN UPDATE ====
    void on_update()
    {
        float now = gametime->get_time();

        // Хоткей E под башню
        if (settings::allow_tower->get_bool() && !prev_tower_hotkey)
            allow_e_tower = !allow_e_tower;
        prev_tower_hotkey = settings::allow_tower->get_bool();

        check_gapcloser();
        check_interruptible();

        if (is_unleashed) {
            if (now >= unleashed_end_time) is_unleashed = false;
            return;
        }
        if (unleash_stacks > 0 && now - last_stack_time > Q_STACK_DURATION) {
            if (stacks_start_decay == 0.0f) { stacks_start_decay = now; last_decay_tick = now; }
            if (now - last_decay_tick >= Q_DECAY_TICK) {
                unleash_stacks--;
                last_decay_tick = now;
                if (unleash_stacks <= 0) {
                    unleash_stacks = 0; stacks_start_decay = 0.0f; last_decay_tick = 0.0f;
                }
            }
        }
        else { stacks_start_decay = 0.0f; last_decay_tick = 0.0f; }

        if (want_post_e_w && w && w->is_ready())
        {
            if (now > want_post_e_w_until - 0.07f)
            {
                auto target = target_selector->get_target(w->range(), damage_type::magical);
                if (is_valid_object(target))
                {
                    auto pred = w->get_prediction(target);
                    if (pred.hitchance >= hit_chance::high)
                    {
                        w->cast(pred.get_cast_position());
                        want_post_e_w = false;
                        return;
                    }
                }
            }
            if (now > want_post_e_w_until)
                want_post_e_w = false;
        }
        try_ks_w_ew();
        try_cast_w_to_cc();
        if (orbwalker->combo_mode()) {
            try_cast_q(); try_cast_w(); try_cast_e(); try_cast_r();
        }
        if (orbwalker->flee_mode()) {
            e_flee();
        }
        else if (orbwalker->lane_clear_mode() || orbwalker->last_hit_mode()) {
            farm_with_q(); farm_with_w();
        }
    }

    // ==== DRAW ====
    void on_draw()
    {
        if (settings::draw_range_w->get_bool())
            draw_manager->add_circle(myhero->get_position(), w->range(), D3DCOLOR_ARGB(255, 227, 203, 20));
        if (settings::draw_range_e->get_bool())
            draw_manager->add_circle(myhero->get_position(), e->range(), D3DCOLOR_ARGB(255, 235, 12, 223));
        vector pos = myhero->get_position(); pos.y += 30.0f;
        bool enabled = settings::farm_hotkey->get_bool();
        auto color = enabled ? D3DCOLOR_ARGB(255, 30, 255, 30) : D3DCOLOR_ARGB(255, 255, 30, 30);
        draw_manager->add_text(pos, color, 16, enabled ? "Farm Q: ON" : "Farm Q: OFF");

        pos.y += 40.0f;
        draw_manager->add_text(pos,
            allow_e_tower ? D3DCOLOR_ARGB(255, 120, 220, 60) : D3DCOLOR_ARGB(255, 220, 60, 60),
            15, allow_e_tower ? "E UnderTower: ON" : "E Undertower: OFF");
    }

    // ==== LOAD / UNLOAD ====
    void load()
    {
        q = plugin_sdk->register_spell(spellslot::q, 650.0f);
        w = plugin_sdk->register_spell(spellslot::w, 1150.0f);
        e = plugin_sdk->register_spell(spellslot::e, 425.0f);
        r = plugin_sdk->register_spell(spellslot::r, 0.0f);
        w->set_skillshot(0.25f, 100.0f, 1600.0f, { collisionable_objects::minions }, skillshot_type::skillshot_line);

        settings::main_tab = menu->create_tab("carry.yunara", "Yunara");
        auto main = settings::main_tab->add_tab("carry.yunara.main", "Main settings");
        settings::auto_q = main->add_checkbox("carry.yunara.main.q", "Combo Q", true);
        settings::auto_w = main->add_checkbox("carry.yunara.main.w", "Combo W", true);
        settings::w_auto_control = main->add_checkbox("carry.yunara.w.auto_control", "Auto W if enemy CC'd", true);
        settings::w_auto_predict = main->add_checkbox("carry.yunara.w.auto_predict", "Auto W (predict)", true);
        settings::auto_e = main->add_checkbox("carry.yunara.main.e", "Combo E", true);
        settings::e_safe_distance = main->add_slider("carry.yunara.e_dist", "E safe distance", 400, 150, 800);
        settings::e_jump_mode = main->add_combobox("carry.yunara.e.jump_mode", "E dash mode",
            { {"To Cursor", nullptr}, {"Side Step", nullptr}, {"Away From Enemy", nullptr}, {"Anti-Melee", nullptr} }, 0);
        settings::e_my_hp_check = main->add_checkbox("carry.yunara.e.myhp_enable", "Use E if my HP below", false);
        settings::e_my_hp = main->add_slider("carry.yunara.e.myhp", "My HP % for E", 30, 1, 100);
        settings::e_enemy_hp_check = main->add_checkbox("carry.yunara.e.enemyhp_enable", "Use E if enemy HP below", false);
        settings::e_enemy_hp = main->add_slider("carry.yunara.e.enemyhp", "Enemy HP % for E", 40, 1, 100);
        settings::e_antimelee_enable = main->add_checkbox("carry.yunara.e.antimelee_enable", "E anti-melee (auto dash away from melee in range)", true);
        settings::e_antimelee_range = main->add_slider("carry.yunara.e.antimelee_range", "Anti-melee range", 300, 100, 600);
        settings::auto_r = main->add_checkbox("carry.yunara.main.r", "Auto R", true);
        settings::r_min_hp = main->add_slider("carry.yunara.r.min_my_hp", "Min my HP% for R", 30, 1, 100);
        settings::r_min_enemies = main->add_slider("carry.yunara.r.min_enemies", "Min enemies for R", 2, 1, 5);
        settings::r_min_enemy_hp = main->add_slider("carry.yunara.r.min_enemy_hp", "Min enemy HP% for R", 40, 1, 100);
        settings::r_skip_if_q_buff = main->add_checkbox("carry.yunara.r.skip_if_q_buff", "Don't R if Q buff active", true);

        auto farm = settings::main_tab->add_tab("carry.yunara.farm", "Farm settings");
        settings::farm_q = farm->add_checkbox("carry.yunara.farm.q", "Farm Q", true);
        settings::farm_hotkey = farm->add_hotkey("carry.yunara.farm.hotkey", "Farm Q Hotkey", TreeHotkeyMode::Toggle, 0x58, true);
        settings::q_aoe_farm_mode = farm->add_checkbox("carry.yunara.q.aoe_mode", "Enable AOE farm mode (care abt Q bounce dmg)", true);
        settings::q_use_if_expiring_farm = farm->add_checkbox("carry.yunara.q.expiring_farm", "Use Q if expiring (farm)", true);
        settings::q_expiring_radius_farm = farm->add_slider("carry.yunara.q.expiring_radius_farm", "Expiring Q farm radius", 600, 200, 1100);
        settings::w_farm_min_targets = farm->add_slider("carry.yunara.w.farm_min_targets", "W min minions/jungle for farm", 2, 1, 5);

        auto combo = settings::main_tab->add_tab("carry.yunara.combo", "Misc settings");
        settings::auto_ks_w_ew = combo->add_checkbox("auto_ks_w_ew", "KillSteal W/EW", true);
        settings::allow_tower = combo->add_hotkey("tower_toggle", "Under tower (On/Off)", 0, 0x54, false);
        settings::q_use_if_expiring_combo = combo->add_checkbox("carry.yunara.q.expiring_combo", "Use Q if expiring (combo)", true);
        settings::q_expiring_radius_combo = combo->add_slider("carry.yunara.q.expiring_radius_combo", "Expiring Q combo radius (how enemy far from u)", 400, 200, 1100);

        auto draw = settings::main_tab->add_tab("carry.yunara.draw", "Draw Settings");
        settings::draw_range_w = draw->add_checkbox("carry.yunara.draw.w", "Draw W range", true);
        settings::draw_range_e = draw->add_checkbox("carry.yunara.draw.e", "Draw E range", true);

        // Новые чекбоксы для расширенных хуков
        auto util = settings::main_tab->add_tab("carry.yunara.util", "Utility");
        settings::enable_antigap = util->add_checkbox("carry.yunara.util.gapcloser", "Enable E Anti-Gapcloser ", true);
        settings::enable_interrupt = util->add_checkbox("carry.yunara.util.interrupt", "Cast W on channeled spells", true);

        event_handler<events::on_update>::add_callback(on_update);
        event_handler<events::on_draw>::add_callback(on_draw);
        event_handler<events::on_after_attack_orbwalker>::add_callback(on_after_attack);
    }

    void unload()
    {
        event_handler<events::on_update>::remove_handler(on_update);
        event_handler<events::on_draw>::remove_handler(on_draw);
        event_handler<events::on_after_attack_orbwalker>::remove_handler(on_after_attack);
        if (settings::main_tab)
        {
            menu->delete_tab(settings::main_tab);
            settings::main_tab = nullptr;
        }
        if (q) { plugin_sdk->remove_spell(q); q = nullptr; }
        if (w) { plugin_sdk->remove_spell(w); w = nullptr; }
        if (e) { plugin_sdk->remove_spell(e); e = nullptr; }
        if (r) { plugin_sdk->remove_spell(r); r = nullptr; }
    }
}
