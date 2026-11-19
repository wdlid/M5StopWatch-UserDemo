/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <lvgl.h>
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>
#include <cstdint>
#include <memory>
#include <string>

namespace view {

/**
 * @brief Curved edge action bar aligned with physical buttons A (top-left) and B (top-right).
 */
class ArcActionBar {
public:
    ArcActionBar(lv_obj_t* parent,
                 const char* leftText  = "Cancel",
                 const char* rightText = "OK",
                 const char* titleText = "",
                 uint32_t leftColor    = 0xFF8A8A,
                 uint32_t rightColor   = 0x4AD78C);
    ~ArcActionBar();

    void update();

    void setLeftText(const char* text, uint32_t color = 0xFF8A8A);
    void setRightText(const char* text, uint32_t color = 0x4AD78C);
    void setTitle(const char* text);

    bool consumeLeftClicked();
    bool consumeRightClicked();

private:
    lv_obj_t* _left_arc  = nullptr;
    lv_obj_t* _right_arc = nullptr;
    std::unique_ptr<uitk::lvgl_cpp::Container> _left_hit;
    std::unique_ptr<uitk::lvgl_cpp::Container> _right_hit;
    std::unique_ptr<uitk::lvgl_cpp::Label> _title_label;

    std::string _left_text;
    std::string _right_text;
    bool _left_clicked  = false;
    bool _right_clicked = false;
};

}  // namespace view
