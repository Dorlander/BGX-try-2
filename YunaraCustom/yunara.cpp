#include "yunara.h"
#include "../plugin_sdk/plugin_sdk.hpp"
#include <string>
#include <algorithm>

namespace yunara
{
    // ---- CONSTS ----
    constexpr float Q_AOE_RADIUS = 260.0f;
    constexpr float Q_STACK_DURATION = 6.0f;
    constexpr float Q_DECAY_TICK = 0.5f;
    constexpr float POST_E_W_DELAY = 0.13f; // Можно подогнать

    // ---- INTERNAL STATE ----
    static script_spell* q = nullptr;
    static script_spell* w = nullptr;
    static script_spell* e = nullptr;
    static script_spell* r = nullptr;

    static int unleash_stacks = 0;
    static float last_stack_time = 0.0f;
    static float stacks_start_decay = 0.0f;
    static float last_decay_tick = 0.0f;
    static bool is_unleashed = false;
    static float unleashed_end_time = 0.0f;

    static bool want_post_e_w = false;
    static float want_post_e_w_until = 0.0f;

    // ---- SETTINGS STRUCTURE ----
    namespace settings
    {
        TreeTab* main_tab = nullptr;
        TreeEntry* farm_q = nullptr, * farm_hotkey = nullptr, * q_aoe_farm_mode = nullptr, * q_use_if_expiring_farm = nullptr,
            * q_expiring_radius_farm = nullptr, * auto_q = nullptr, * q_use_if_expiring_combo = nullptr, * q_expiring_radius_combo = nullptr,
            * auto_w = nullptr, * w_auto_control = nullptr, * w_auto_predict = nullptr, * w_farm_min_targets = nullptr,
            * auto_e = nullptr, * e_safe_distance = nullptr, * e_jump_mode = nullptr, * e_my_hp_check = nullptr, * e_my_hp = nullptr,
            * e_enemy_hp_check = nullptr, * e_enemy_hp = nullptr, * e_antimelee_enable = nullptr, * e_antimelee_range = nullptr,
            * auto_r = nullptr, * r_min_hp = nullptr, * r_min_enemies = nullptr, * r_min_enemy_hp = nullptr, * r_skip_if_q_buff = nullptr,
            * draw_range_w = nullptr, * draw_range_e = nullptr;
    }

    // --- INTERNAL HELPERS ---

    // Чек баффа по имени (без регистра)
    bool has_buff_by_name(const game_object_script& obj, const std::string& name)
    {
        for (const auto& buff : obj->get_bufflist())
            if (buff && buff->is_valid() && _stricmp(buff->get_name().c_str(), name.c_str()) == 0)
                return true;
        return false;
    }

    // Бафф ульты (Юнара)
    bool is_r_active()
    {
        // Имя уточни через вывод из try_cast_r
        return has_buff_by_name(myhero, "YunaraR");
    }

    // --- Q ----
    void after_q_cast()
    {
        is_unleashed = true;
        unleash_stacks = 0;
        unleashed_end_time = gametime->get_time() + Q_STACK_DURATION;
    }

    void on_after_attack(game_object_script target)
    {
        if (!target || myhero->is_dead())
            return;

        // Stack logic
        unleash_stacks += target->is_ai_hero() ? 2 : 1;
        if (unleash_stacks > 8) unleash_stacks = 8;
        last_stack_time = gametime->get_time();

        // Q авто-ресет после АА
        if (q && q->is_ready() && target->obj_is_attackable())
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

        // Expiring stacks
        if (settings::q_use_if_expiring_farm->get_bool() && unleash_stacks >= 8 && !is_unleashed) {
            float now = gametime->get_time();
            float exp_time = last_stack_time + Q_STACK_DURATION - now;
            if (exp_time < 0.7f) {
                for (auto& minion : minions)
                    if (minion->is_valid_target(settings::q_expiring_radius_farm->get_int()) && q->cast())
                    {
                        after_q_cast(); break;
                    }
            }
        }
        // AOE farm
        if (settings::q_aoe_farm_mode->get_bool())
        {
            int minion_threshold = 2;
            for (auto& main : minions) {
                if (!main->is_valid_target(q->range())) continue;
                int aoe_count = 0;
                vector hero_to_main = main->get_position() - myhero->get_position();
                for (auto& other : minions) {
                    if (other == main || !other->is_valid_target(q->range())) continue;
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
            for (auto& minion : minions)
                if (minion->is_valid_target(q->range()) && q->cast()) { after_q_cast(); break; }
        }
    }

    void try_cast_q()
    {
        if (is_unleashed || !settings::auto_q->get_bool() || !q->is_ready() || unleash_stacks < 8) return;
        for (auto& enemy : entitylist->get_enemy_heroes())
        {
            if (enemy && enemy->is_valid() && !enemy->is_dead() &&
                enemy->get_distance(myhero) <= myhero->get_attack_range() + myhero->get_bounding_radius() + 50.0f)
            {
                if (q->cast()) after_q_cast();
                break;
            }
        }
        if (settings::q_use_if_expiring_combo->get_bool() && unleash_stacks >= 8 && !is_unleashed)
        {
            float now = gametime->get_time();
            float exp_time = last_stack_time + Q_STACK_DURATION - now;
            if (exp_time < 0.5f) {
                for (auto& enemy : entitylist->get_enemy_heroes()) {
                    if (enemy && enemy->is_valid() && !enemy->is_dead() &&
                        enemy->get_distance(myhero) <= settings::q_expiring_radius_combo->get_int() && enemy->is_visible()) {
                        if (q->cast()) { after_q_cast(); break; }
                    }
                }
            }
        }
    }

    // --- W ----
    void try_cast_w()
    {
        if (!settings::auto_w->get_bool() || !w->is_ready()) return;

        if (settings::w_auto_control->get_bool()) {
            for (auto& enemy : entitylist->get_enemy_heroes()) {
                if (!enemy->is_valid_target(w->range())) continue;
                if (enemy->has_buff_type(buff_type::Stun) || enemy->has_buff_type(buff_type::Snare) ||
                    enemy->has_buff_type(buff_type::Fear) || enemy->has_buff_type(buff_type::Slow)) {
                    auto pred = w->get_prediction(enemy);
                    if (pred.hitchance >= hit_chance::very_high)
                        w->cast(pred.get_cast_position());
                    return;
                }
            }
        }
        if (settings::w_auto_predict->get_bool()) {
            auto target = target_selector->get_target(w->range(), damage_type::magical);
            if (target && target->is_valid() && !target->is_dead()) {
                auto pred = w->get_prediction(target);
                if (pred.hitchance >= hit_chance::high)
                    w->cast(pred.get_cast_position());
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
            if (!main->is_valid_target(w->range())) continue;
            int count = 1;
            vector pos = main->get_position();
            for (auto& other : minions)
            {
                if (other == main || !other->is_valid_target(w->range())) continue;
                vector to_other = other->get_position() - pos;
                float dist = sqrtf(to_other.x * to_other.x + to_other.y * to_other.y);
                if (dist < 100.0f) count++;
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

    // --- E ----
    enum class e_mode_t { ToCursor, Side, Away, AntiMelee };

    void try_cast_e()
    {
        // Anti-melee logic (первая)
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
                float len = sqrtf(from_melee.x * from_melee.x + from_melee.y * from_melee.y);
                if (len > 0.001f)
                {
                    from_melee.x /= len;
                    from_melee.y /= len;
                    float dash_range = settings::e_safe_distance ? (float)settings::e_safe_distance->get_int() : 350.0f;
                    vector dash_pos = myhero->get_position() + from_melee * dash_range;
                    if (e->cast(dash_pos) && is_r_active())
                    {
                        want_post_e_w = true;
                        want_post_e_w_until = gametime->get_time() + POST_E_W_DELAY;
                    }
                    return;
                }
            }
        }
        // Стандартные проверки
        if (!settings::auto_e || !settings::auto_e->get_bool() || !e || !e->is_ready() || !myhero || myhero->is_dead())
            return;
        if (!game_input || !renderer) return;

        int mode = 0;
        float dash_range = settings::e_safe_distance ? (float)settings::e_safe_distance->get_int() : 350.0f;
        if (settings::e_jump_mode) mode = settings::e_jump_mode->get_int();

        vector hero_pos = myhero->get_position();
        vector dash_pos = hero_pos;

        // --- Найти ближайшего врага
        game_object_script closest = nullptr;
        float min_dist = FLT_MAX;
        for (auto& enemy : entitylist->get_enemy_heroes())
        {
            if (!enemy || !enemy->is_valid_target(700.0f)) continue;
            float d = myhero->get_distance(enemy);
            if (d < min_dist) { min_dist = d; closest = enemy; }
        }

        // --- HP CHECKS
        bool allow_e = true;
        if (settings::e_my_hp_check && settings::e_my_hp_check->get_bool())
            if (myhero->get_health_percent() >= settings::e_my_hp->get_int())
                allow_e = false;
        if (settings::e_enemy_hp_check && settings::e_enemy_hp_check->get_bool() && closest)
            if (closest->get_health_percent() >= settings::e_enemy_hp->get_int())
                allow_e = false;
        if (!allow_e)
            return;

        // --- Dash logic ---
        if (mode == 0) // Курсор через screen_to_world
        {
            point2 mouse = game_input->get_game_cursor_pos();
            vector screen_mouse((float)mouse.x, (float)mouse.y, 0.0f);
            vector cursor_world;
            renderer->screen_to_world(screen_mouse, cursor_world);

            vector dir = cursor_world - hero_pos;
            float len = sqrtf(dir.x * dir.x + dir.y * dir.y);
            if (len < 0.001f) return;
            dir.x /= len; dir.y /= len;
            dash_pos = hero_pos + dir * dash_range;
        }
        else if (mode == 1 && closest) // Side step: ближайший перпендикуляр к world-курсору
        {
            vector enemy_dir = hero_pos - closest->get_position();
            float len = sqrtf(enemy_dir.x * enemy_dir.x + enemy_dir.y * enemy_dir.y);
            if (len < 0.001f) return;
            enemy_dir.x /= len; enemy_dir.y /= len;

            vector perp1(-enemy_dir.y, enemy_dir.x, 0.f); // влево
            vector perp2(enemy_dir.y, -enemy_dir.x, 0.f); // вправо

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
        else if (mode == 2 && closest) // ОТ врага
        {
            vector from_enemy = hero_pos - closest->get_position();
            float len = sqrtf(from_enemy.x * from_enemy.x + from_enemy.y * from_enemy.y);
            if (len < 0.001f) return;
            from_enemy.x /= len; from_enemy.y /= len;
            dash_pos = hero_pos + from_enemy * dash_range;
        }
        else if (mode == 3 && closest && min_dist < 350.f) // ANTI-MELEE (если враг рядом)
        {
            vector from_enemy = hero_pos - closest->get_position();
            float len = sqrtf(from_enemy.x * from_enemy.x + from_enemy.y * from_enemy.y);
            if (len < 0.001f) return;
            from_enemy.x /= len; from_enemy.y /= len;
            dash_pos = hero_pos + from_enemy * dash_range;
        }
        else return;

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

        float len = sqrtf(dir.x * dir.x + dir.y * dir.y);
        if (len < 0.001f) return;

        dir.x /= len;
        dir.y /= len;

        float dash_range = settings::e_safe_distance ? (float)settings::e_safe_distance->get_int() : 400.0f;
        vector dash_pos = hero_pos + dir * dash_range;

        e->cast(dash_pos);
    }

    // --- R ---
    void try_cast_r()
    {
        if (!settings::auto_r->get_bool() || !r->is_ready()) return;
        if (settings::r_skip_if_q_buff->get_bool() && is_unleashed) return;
        if (myhero->get_health_percent() < settings::r_min_hp->get_int()) return;

        int enemy_count = 0;
        for (auto& hero : entitylist->get_enemy_heroes()) {
            if (hero && hero->is_valid() && !hero->is_dead() &&
                hero->get_distance(myhero) < 700.0f &&
                hero->get_health_percent() < settings::r_min_enemy_hp->get_int())
                enemy_count++;
        }
        if (enemy_count >= settings::r_min_enemies->get_int())
            r->cast();

        // Для дебага — смотри, что за баффы вообще есть:
        for (auto& buff : myhero->get_bufflist())
            if (buff && buff->is_valid())
                console->print("[BUFF] %s", buff->get_name().c_str());
    }

    // --- STACK DECAY & UPDATE ---
    void on_update()
    {
        float now = gametime->get_time();

        if (is_unleashed) {
            if (now >= unleashed_end_time)
                is_unleashed = false;
            return;
        }
        if (unleash_stacks > 0 && now - last_stack_time > Q_STACK_DURATION) {
            if (stacks_start_decay == 0.0f) {
                stacks_start_decay = now;
                last_decay_tick = now;
            }
            if (now - last_decay_tick >= Q_DECAY_TICK) {
                unleash_stacks--;
                last_decay_tick = now;
                if (unleash_stacks <= 0) {
                    unleash_stacks = 0;
                    stacks_start_decay = 0.0f;
                    last_decay_tick = 0.0f;
                }
            }
        }
        else {
            stacks_start_decay = 0.0f;
            last_decay_tick = 0.0f;
        }

        // --- POST E->W ульта ---
        if (want_post_e_w && w && w->is_ready())
        {
            if (now > want_post_e_w_until - 0.07f)
            {
                auto target = target_selector->get_target(w->range(), damage_type::magical);
                if (target && target->is_valid() && !target->is_dead())
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

        if (orbwalker->combo_mode()) {
            try_cast_q();
            try_cast_w();
            try_cast_e();
            try_cast_r();
        }
        if (orbwalker->flee_mode()) {
            e_flee();
        }
        else if (orbwalker->lane_clear_mode() || orbwalker->last_hit_mode()) {
            farm_with_q();
            farm_with_w();
        }
    }

    // --- DRAW ---
    void on_draw()
    {
        if (settings::draw_range_w->get_bool())
            draw_manager->add_circle(myhero->get_position(), w->range(), D3DCOLOR_ARGB(255, 227, 203, 20));
        if (settings::draw_range_e->get_bool())
            draw_manager->add_circle(myhero->get_position(), e->range(), D3DCOLOR_ARGB(255, 235, 12, 223));

        vector pos = myhero->get_position();
        pos.y += 30.0f;

        bool enabled = settings::farm_hotkey->get_bool();

        auto color = enabled
            ? D3DCOLOR_ARGB(255, 30, 255, 30)
            : D3DCOLOR_ARGB(255, 255, 30, 30);

        draw_manager->add_text(
            pos,
            color,
            16,
            enabled ? "Farm Q: ON" : "Farm Q: OFF"
        );
    }

    // --- LOAD/UNLOAD ---
    void load()
    {
        q = plugin_sdk->register_spell(spellslot::q, 650.0f);
        w = plugin_sdk->register_spell(spellslot::w, 1150.0f);
        e = plugin_sdk->register_spell(spellslot::e, 425.0f);
        r = plugin_sdk->register_spell(spellslot::r, 0.0f);

        w->set_skillshot(0.25f, 100.0f, 1600.0f, { collisionable_objects::minions }, skillshot_type::skillshot_line);

        // --- МЕНЮ ---
        settings::main_tab = menu->create_tab("carry.yunara", "Yunara");

        auto main = settings::main_tab->add_tab("carry.yunara.main", "Main settings");
        settings::auto_q = main->add_checkbox("carry.yunara.main.q", "Combo Q", true);
        settings::auto_w = main->add_checkbox("carry.yunara.main.w", "Combo W", true);
        settings::w_auto_control = main->add_checkbox("carry.yunara.w.auto_control", "Auto W if enemy CC'd", true);
        settings::w_auto_predict = main->add_checkbox("carry.yunara.w.auto_predict", "Auto W (predict)", true);
        settings::auto_e = main->add_checkbox("carry.yunara.main.e", "Combo E", true);
        settings::e_safe_distance = main->add_slider("carry.yunara.e_dist", "E safe distance", 400, 150, 800);
        settings::e_jump_mode = main->add_combobox(
            "carry.yunara.e.jump_mode", "E dash mode",
            { {"To Cursor", nullptr}, {"Side Step", nullptr}, {"Away From Enemy", nullptr}, {"Anti-Melee", nullptr} },
            0
        );
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
        settings::w_farm_min_targets = farm->add_slider(
            "carry.yunara.w.farm_min_targets", "W min minions/jungle for farm", 2, 1, 5
        );

        auto combo = settings::main_tab->add_tab("carry.yunara.combo", "Misc settings");
        settings::q_use_if_expiring_combo = combo->add_checkbox("carry.yunara.q.expiring_combo", "Use Q if expiring (combo)", true);
        settings::q_expiring_radius_combo = combo->add_slider("carry.yunara.q.expiring_radius_combo", "Expiring Q combo radius (how enemy far from u)", 400, 200, 1100);

        auto draw = settings::main_tab->add_tab("carry.yunara.draw", "Draw Settings");
        settings::draw_range_w = draw->add_checkbox("carry.yunara.draw.w", "Draw W range", true);
        settings::draw_range_e = draw->add_checkbox("carry.yunara.draw.e", "Draw E range", true);

        event_handler<events::on_update>::add_callback(on_update);
        event_handler<events::on_draw>::add_callback(on_draw);
        event_handler<events::on_after_attack_orbwalker>::add_callback(on_after_attack);
    }

    void unload()
    {
        menu->delete_tab(settings::main_tab);
        plugin_sdk->remove_spell(q);
        plugin_sdk->remove_spell(w);
        plugin_sdk->remove_spell(e);
        plugin_sdk->remove_spell(r);
        event_handler<events::on_update>::remove_handler(on_update);
        event_handler<events::on_draw>::remove_handler(on_draw);
        event_handler<events::on_after_attack_orbwalker>::remove_handler(on_after_attack);
    }
} // end namespace yunara
