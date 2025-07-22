#include "yunara.h"
#include "../plugin_sdk/plugin_sdk.hpp"

static script_spell* q = nullptr;
static script_spell* w = nullptr;
static script_spell* e = nullptr;
static script_spell* r = nullptr;

static int unleash_stacks = 0;
static float last_stack_time = 0.0f;

float distance_point_to_line(const vector& pt, const vector& line_start, const vector& line_end)
{
    // Вектор от line_start к pt
    vector v = pt - line_start;
    // Вектор от line_start к line_end
    vector u = line_end - line_start;

    float len = u.length();
    if (len == 0.f) return (pt - line_start).length();

    float t = (v.x * u.x + v.y * u.y + v.z * u.z) / (len * len);
    t = std::max(0.f, std::min(1.f, t));

    vector projection = line_start + u * t;
    return (pt - projection).length();
}





#define Q_DRAW_COLOR (D3DCOLOR_ARGB(255, 62, 129, 237))
#define W_DRAW_COLOR (D3DCOLOR_ARGB(255, 227, 203, 20))
#define E_DRAW_COLOR (D3DCOLOR_ARGB(255, 235, 12, 223))
#define R_DRAW_COLOR (D3DCOLOR_ARGB(255, 224, 77, 13))

namespace settings
{
    TreeTab* main_tab = nullptr;
    TreeEntry* auto_q = nullptr;
    TreeEntry* auto_w = nullptr;
    TreeEntry* auto_e = nullptr;
    TreeEntry* auto_r = nullptr;
    TreeEntry* r_enemy_slider = nullptr;
    TreeEntry* w_range = nullptr;
}

namespace draw_settings
{
    TreeEntry* draw_range_q = nullptr;
    TreeEntry* draw_range_w = nullptr;
    TreeEntry* draw_range_e = nullptr;
    TreeEntry* draw_range_r = nullptr;
}

namespace farm_settings
{
    TreeEntry* farm_q = nullptr;
    TreeEntry* farm_q_min_aoe = nullptr;
    TreeEntry* farm_w = nullptr;
    TreeEntry* farm_w_min = nullptr; 
}



constexpr float Q_RANGE = 650.0f;
constexpr float W_RANGE = 1150.0f;
constexpr float W_WIDTH = 100.0f;
constexpr float W_SPEED = 1600.0f;
constexpr float E_RANGE = 500.0f;

void try_cast_q()
{
    if (!settings::auto_q->get_bool() || !q->is_ready()) return;
    if (unleash_stacks >= 8)
    {
        for (auto& enemy : entitylist->get_enemy_heroes())
        {
            if (enemy && enemy->is_valid() && !enemy->is_dead() &&
                enemy->get_distance(myhero) <= myhero->get_attack_range() + myhero->get_bounding_radius() + 50.0f)
            {
                q->cast();
                unleash_stacks = 0; // Сбрасываем после каста
                break;
            }
        }
    }
}





void try_cast_w()
{
    if (!settings::auto_w->get_bool() || !w->is_ready()) return;
    float max_range = static_cast<float>(settings::w_range->get_int());
    auto target = target_selector->get_target(max_range, damage_type::magical);
    if (target && target->is_valid() && !target->is_dead())
        w->cast(target);
}


void try_cast_e()
{
    if (!settings::auto_e->get_bool() || !e->is_ready()) return;
    auto target = target_selector->get_target(700.0f, damage_type::physical);
    if (!target || !target->is_valid() || target->is_dead()) return;

    if (myhero->has_buff(buff_hash("YunaraRbuff")))
    {
        if (myhero->get_distance(target) <= E_RANGE + 100.0f)
            e->cast(target->get_position());
    }
    else
    {
        float chase_range = myhero->get_attack_range() + 200.0f;
        if (target->get_distance(myhero) > chase_range)
            e->cast();
    }
}

void try_cast_r()
{
    if (!settings::auto_r->get_bool() || !r->is_ready()) return;
    int enemies = 0;
    for (auto& hero : entitylist->get_enemy_heroes())
        if (hero && hero->is_valid() && !hero->is_dead() &&
            hero->get_distance(myhero) < 700.0f)
            enemies++;
    if (enemies >= settings::r_enemy_slider->get_int())
        r->cast();
}

void farm_with_q()
{
    if (!farm_settings::farm_q->get_bool() || !q->is_ready() || unleash_stacks < 8)
        return;

    const float Q_AOE_RANGE = 250.0f;
    const int minion_threshold = 2;

    auto minions = entitylist->get_enemy_minions();

    for (auto& main : minions)
    {
        if (!main->is_valid_target(q->range()))
            continue;

        int aoe_count = 0;
        vector hero_to_main = main->get_position() - myhero->get_position();

        for (auto& other : minions)
        {
            if (other == main || !other->is_valid_target(q->range()))
                continue;

            vector main_to_other = other->get_position() - main->get_position();

            float forward = (hero_to_main.x * main_to_other.x + hero_to_main.y * main_to_other.y);
            if (forward <= 0) // только позади main относительно героя
                continue;

            float dist = sqrtf(main_to_other.x * main_to_other.x + main_to_other.y * main_to_other.y);
            if (dist < Q_AOE_RANGE)
                aoe_count++;
        }

        // Если позади стоит минимум два миньона — жмём Q
        if (aoe_count >= minion_threshold)
        {
            if (q->cast())
                unleash_stacks = 0;
            break;
        }
    }
}





void farm_with_w()
{
    if (!farm_settings::farm_w->get_bool() || !w->is_ready())
        return;

    float max_range = static_cast<float>(settings::w_range->get_int());
    int min_minions = farm_settings::farm_w_min->get_int();

    auto minions = entitylist->get_enemy_minions();
    int best_count = 0;
    vector best_pos;

    for (auto& minion : minions)
    {
        if (!minion->is_valid_target(max_range))
            continue;

        int count = 1;
        vector cast_from = myhero->get_position();
        vector cast_to = minion->get_position();

        for (auto& other : minions)
        {
            if (other == minion || !other->is_valid_target(max_range))
                continue;

            float dist = distance_point_to_line(other->get_position(), cast_from, cast_to);
            if (dist < W_WIDTH)
                count++;
        }
        if (count > best_count)
        {
            best_count = count;
            best_pos = cast_to;
        }
    }

    if (best_count >= min_minions)
        w->cast(best_pos);
}







void on_draw()
{
    if (draw_settings::draw_range_q->get_bool())
        draw_manager->add_circle(myhero->get_position(), q->range(), Q_DRAW_COLOR);
    if (draw_settings::draw_range_w->get_bool())
        draw_manager->add_circle(myhero->get_position(), w->range(), W_DRAW_COLOR);
    if (draw_settings::draw_range_e->get_bool())
        draw_manager->add_circle(myhero->get_position(), e->range(), E_DRAW_COLOR);
    if (draw_settings::draw_range_r->get_bool())
        draw_manager->add_circle(myhero->get_position(), r->range(), R_DRAW_COLOR);
    draw_manager->add_text_on_screen({ 30, 80 }, MAKE_COLOR(255, 255, 0, 255), 18,
        ("Yunara Q stacks: " + std::to_string(unleash_stacks)).c_str());

}

void on_update()
{
    if (!myhero || myhero->is_dead()) return;

    // Чётко по вики: баф длится 6 сек
    if (unleash_stacks > 0 && gametime->get_time() - last_stack_time >= 6.0f)
        unleash_stacks = 0;

    if (orbwalker->combo_mode())
    {
        try_cast_q();
        try_cast_w();
        try_cast_e();
        try_cast_r();
    }
    else if (orbwalker->lane_clear_mode())
    {
        farm_with_q();
        farm_with_w();
    }
}


void on_after_attack(game_object_script target)
{
    if (target->is_ai_hero())
        unleash_stacks += 2;
    else if (target->is_ai_minion())
        unleash_stacks += 1;
    if (unleash_stacks > 8)
        unleash_stacks = 8;
    last_stack_time = gametime->get_time();
}


void yunara::load()
{
    q = plugin_sdk->register_spell(spellslot::q, Q_RANGE);
    w = plugin_sdk->register_spell(spellslot::w, W_RANGE);
    e = plugin_sdk->register_spell(spellslot::e, E_RANGE);
    r = plugin_sdk->register_spell(spellslot::r, 0.0f);

    w->set_skillshot(0.25f, W_WIDTH, W_SPEED, { collisionable_objects::minions }, skillshot_type::skillshot_line);

    settings::main_tab = menu->create_tab("carry.yunara", "Yunara");

    auto main = settings::main_tab->add_tab("carry.yunara.main", "Main settings");
    settings::auto_q = main->add_checkbox("carry.yunara.main.q", "Auto Q", true);
    settings::auto_w = main->add_checkbox("carry.yunara.main.w", "Auto W", true);
    settings::auto_e = main->add_checkbox("carry.yunara.main.e", "Auto E", true);
    settings::auto_r = main->add_checkbox("carry.yunara.main.r", "Auto R", true);
    settings::r_enemy_slider = main->add_slider("carry.yunara.main.r_slider", "Auto R if X enemys near >=", 2, 1, 5);
    settings::w_range = main->add_slider("carry.yunara.main.whc", "W Range", 1150, 0, 1150);

    auto farm = settings::main_tab->add_tab("carry.yunara.farm", "Farm settings");
    farm_settings::farm_q = farm->add_checkbox("carry.yunara.farm.q", "Farm Q", true);
    farm_settings::farm_q_min_aoe = farm->add_slider(
        "carry.yunara.farm.q_min_aoe", "Min minions for Q AOE farm", 2, 0, 8);

    farm_settings::farm_w = farm->add_checkbox("carry.yunara.farm.w", "Farm W", true);
   
    farm_settings::farm_w_min = farm->add_slider(
        "carry.yunara.farm.w_min", "Min minions for W farm", 3, 0, 8);

    auto draw = settings::main_tab->add_tab("carry.yunara.draw", "Draw Settings");
    draw_settings::draw_range_q = draw->add_checkbox("carry.yunara.draw.q", "Draw Q range", true);
    draw_settings::draw_range_w = draw->add_checkbox("carry.yunara.draw.w", "Draw W range", true);
    draw_settings::draw_range_e = draw->add_checkbox("carry.yunara.draw.e", "Draw E range", true);
    draw_settings::draw_range_r = draw->add_checkbox("carry.yunara.draw.r", "Draw R range", true);


    event_handler<events::on_update>::add_callback(on_update);
    event_handler<events::on_draw>::add_callback(on_draw);
    event_handler<events::on_after_attack_orbwalker>::add_callback(on_after_attack);

}


void yunara::unload()
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
