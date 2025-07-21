#include "yunara.h"

namespace yunara
{
    // Spell declarations
    script_spell* q = nullptr;
    script_spell* w = nullptr;
    script_spell* e = nullptr;
    script_spell* e2 = nullptr;
    script_spell* r = nullptr;

    TreeTab* main_tab = menu->create_tab("yunara", "Yunara Custom");

    namespace combo
    {
        TreeEntry* use_q = nullptr;
        TreeEntry* use_w = nullptr;
        TreeEntry* use_e = nullptr;
        TreeEntry* use_r = nullptr;
    }

    namespace harass
    {
        TreeEntry* use_q = nullptr;
        TreeEntry* use_w = nullptr;
    }

    namespace draw_settings
    {
        TreeEntry* draw_q = nullptr;
        TreeEntry* draw_w = nullptr;
        TreeEntry* draw_e = nullptr;
        TreeEntry* draw_r = nullptr;
    }

    namespace misc
    {
        TreeEntry* show_passive = nullptr;
        TreeEntry* debug_output = nullptr;
    }

    int passive_stacks = 0;
    float last_stack_time = 0.0f;
    float last_decay_time = 0.0f;
    bool r_active = false;

    void on_update()
    {
        if (myhero->is_dead()) return;
        handle_passive();

        if (orbwalker->combo_mode())
        {
            if (combo::use_r->get_bool() && r->is_ready())
                r_logic();

            if (combo::use_q->get_bool())
                q_logic();

            if (combo::use_w->get_bool())
                w_logic();

            if (combo::use_e->get_bool())
                e_logic();
        }
        else if (orbwalker->harass())
        {
            if (harass::use_q->get_bool())
                q_logic();

            if (harass::use_w->get_bool())
                w_logic();
        }
    }

    void handle_passive()
    {
        if (orbwalker->get_last_target() != nullptr)
        {
            game_object_script target = orbwalker->get_last_target();
            if (target->is_valid_target())
            {
                passive_stacks = std::min(8, passive_stacks + (target->is_ai_hero() ? 2 : 1));
                last_stack_time = gametime->get_time();
            }
        }

        if (gametime->get_time() - last_decay_time > 0.5f && gametime->get_time() - last_stack_time > 6.0f)
        {
            passive_stacks = std::max(0, passive_stacks - 1);
            last_decay_time = gametime->get_time();
        }
    }

    void q_logic()
    {
        if (is_transcendent())
        {
            if (!myhero->has_buff(buff_hash("YunaraQBuff")))
            {
                q->cast();
                passive_stacks = 0;
            }
            return;
        }

        if (passive_stacks >= 8 && q->is_ready())
        {
            q->cast();
            passive_stacks = 0;
        }
    }

    void w_logic()
    {
        auto target = target_selector->get_target(w->range(), damage_type::magical);
        if (!target || !target->is_valid_target()) return;

        if (target->has_buff_type(buff_type::Stun) ||
            target->has_buff_type(buff_type::Snare) ||
            target->has_buff_type(buff_type::Slow))
        {
            w->cast(target->get_position());
        }
    }

    void e_logic()
    {
        auto target = target_selector->get_target(800, damage_type::physical);
        if (!target || !target->is_valid_target()) return;

        if (is_transcendent() && e2 && e2->is_ready())
        {
            e2->cast(target->get_position());
        }
        else if (e && e->is_ready())
        {
            e->cast();
        }
    }

    void r_logic()
    {
        if (!r_active)
            enter_transcendence();
        else
            exit_transcendence();
    }

    void enter_transcendence()
    {
        r->cast();
        r_active = true;
    }

    void exit_transcendence()
    {
        r_active = false;
    }

    void on_draw()
    {
        if (draw_settings::draw_q->get_bool())
            draw_manager->add_circle(myhero->get_position(), 0, 255);

        if (draw_settings::draw_w->get_bool())
            draw_manager->add_circle(myhero->get_position(), w->range(), 200);

        if (draw_settings::draw_e->get_bool())
            draw_manager->add_circle(myhero->get_position(), 450, 111);

        if (draw_settings::draw_r->get_bool())
            draw_manager->add_circle(myhero->get_position(), 0, 222);
    }

    void on_create(game_object_script obj) {}
    void on_before_attack(game_object_script target, bool* process) {}
    void on_attack(game_object_script target) {}

    void load()
    {
        q = plugin_sdk->register_spell(spellslot::q, 300);
        w = plugin_sdk->register_spell(spellslot::w, 1150);
        e = plugin_sdk->register_spell(spellslot::e,0);
        e2 = plugin_sdk->register_spell(spellslot::e, 450);
        r = plugin_sdk->register_spell(spellslot::r,0);

        combo::use_q = main_tab->add_checkbox("combo_q", "Use Q", true);
        combo::use_w = main_tab->add_checkbox("combo_w", "Use W", true);
        combo::use_e = main_tab->add_checkbox("combo_e", "Use E", true);
        combo::use_r = main_tab->add_checkbox("combo_r", "Use R", true);

        harass::use_q = main_tab->add_checkbox("harass_q", "Use Q in Harass", true);
        harass::use_w = main_tab->add_checkbox("harass_w", "Use W in Harass", false);

        draw_settings::draw_q = main_tab->add_checkbox("draw_q", "Draw Q Range", true);
        draw_settings::draw_w = main_tab->add_checkbox("draw_w", "Draw W Range", true);
        draw_settings::draw_e = main_tab->add_checkbox("draw_e", "Draw E Range", true);
        draw_settings::draw_r = main_tab->add_checkbox("draw_r", "Draw R Range", false);

        event_handler<events::on_update>::add_callback(on_update);
        event_handler<events::on_draw>::add_callback(on_draw);
    }

    void unload()
    {
        plugin_sdk->remove_spell(q);
        plugin_sdk->remove_spell(w);
        plugin_sdk->remove_spell(e);
        plugin_sdk->remove_spell(e2);
        plugin_sdk->remove_spell(r);

        event_handler<events::on_update>::remove_handler(on_update);
        event_handler<events::on_draw>::remove_handler(on_draw);

        menu->delete_tab(main_tab);
    }

   
}
