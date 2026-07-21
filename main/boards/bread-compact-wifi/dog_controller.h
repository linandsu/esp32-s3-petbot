#ifndef DOG_CONTROLLER_H
#define DOG_CONTROLLER_H

#include "pet_dog.h"

// 负责把 PetDog 的动作能力注册为 MCP 工具，并在设备状态变化时驱动小狗的
// 待机/唤醒联动（睡觉/站立），替代旧版里 application.cc 中散落的调用。
class DogController {
public:
    DogController(gpio_num_t servo_io_1, gpio_num_t servo_io_2, gpio_num_t servo_io_3, gpio_num_t servo_io_4);

private:
    PetDog dog_;

    void RegisterStateChangeListener();
    void RegisterMcpTools();
};

#endif // DOG_CONTROLLER_H
