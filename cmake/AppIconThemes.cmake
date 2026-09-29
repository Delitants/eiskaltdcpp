function(eiskalt_add_icon_themes target)
    set(root "${CMAKE_SOURCE_DIR}/eiskaltdcpp-qt/icons/appl")
    file(GLOB_RECURSE icons CONFIGURE_DEPENDS
        "${root}/prism/*.svg" "${root}/contour/*.svg"
        "${root}/slate/*.svg" "${root}/reborn/*.svg")
    qt6_add_resources(${target} modern_icon_themes
        PREFIX "/icon-themes" BASE "${root}" FILES ${icons})
    set(brand_icons)
    foreach(name icon_appl icon_appl_big icon_msg icon_msg_big qt-logo)
        list(APPEND brand_icons "${root}/apex/${name}.png")
    endforeach()
    qt6_add_resources(${target} original_brand_icons
        PREFIX "/icon-themes/apex-brand" BASE "${root}/apex" FILES ${brand_icons})
endfunction()
