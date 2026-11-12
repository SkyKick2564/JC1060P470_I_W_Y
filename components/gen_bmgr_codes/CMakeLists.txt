# Board information output
message(STATUS "Selected Board: jc1060p470_i_w_y")
message(STATUS "Board Path: C:/ESP32P4/JC1060P470_I_W_Y/managed_components/guition-jc__guition_boards/jc1060p470_i_w_y")

idf_component_register(
    SRC_DIRS "." "../../managed_components/guition-jc__guition_boards/jc1060p470_i_w_y"
    INCLUDE_DIRS "." "../../managed_components/guition-jc__guition_boards/jc1060p470_i_w_y"
    REQUIRES esp_board_manager
)

# This is equivalent to adding WHOLE_ARCHIVE option to the idf_component_register call above:
idf_component_set_property(${COMPONENT_NAME} WHOLE_ARCHIVE TRUE)
