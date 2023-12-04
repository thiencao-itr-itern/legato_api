/**
 * @file gpioPin.c
 *
 * This is sample Legato CF3 GPIO app by using le_gpio.api.
 *
 * <HR>
 *
 * Copyright (C) Sierra Wireless, Inc.
 */

/* Legato Framework */
#include "legato.h"
#include "interfaces.h"
//#include "./build/wp76xx/system/api/ea9b3de95059df7b2ad8624f13693cb1/server/le_gpioPin23_server.h"

static int Pin22 = 22;

static void Pin22ChangeCallback(bool state, void *ctx){
    LE_INFO("State change %s", state?"TRUE":"FALSE");

    LE_INFO("Context pointer came back as %d", *(int *)ctx);
}

// -------------------------------------------------------------------------------------------------
/**
 *  Pin-per-service GPIO pin21 as example
 */
// -------------------------------------------------------------------------------------------------
// static void Pin21GpioSignal()
// {
//     bool active;
    
//     LE_INFO("Pin 21 is input = %s", le_gpioPin21_IsInput()?"true":"false");
//     LE_INFO("Pin 21 is output = %s", le_gpioPin21_IsOutput()?"true":"false");

//     le_gpioPin21_Activate();
//     le_gpioPin21_EnablePullUp();
//     le_gpioPin21_Deactivate();

//     LE_INFO("Pin 21 is input (1.2)= %s", le_gpioPin21_IsInput()?"true":"false");
//     LE_INFO("Pin 21 is output (1.2)= %s", le_gpioPin21_IsOutput()?"true":"false");


//     le_gpioPin21_SetInput(0);
//     active = le_gpioPin21_Read();
//     LE_INFO("Pin21 read active (2): %d", active);
//     LE_INFO("Pin 21 is input (2) = %s", le_gpioPin21_IsInput()?"true":"false");
//     LE_INFO("Pin 21 is output (2)= %s", le_gpioPin21_IsOutput()?"true":"false");

//     le_gpioPin21_Polarity_t pol = le_gpioPin21_GetPolarity();
//     if (pol == LE_GPIOPIN21_ACTIVE_HIGH)
//     {
//         LE_INFO("Pin 21 polarity = ACTIVE_HIGH");
//     }
//     else
//     {
//         LE_INFO("Pin 21 polarity = ACTIVE_LOW");
//     }
    
//     le_gpioPin21_SetPushPullOutput(LE_GPIOPIN21_ACTIVE_HIGH, true);
//     LE_INFO("Pin21 read PP - High: %d", le_gpioPin21_Read());

//     le_gpioPin21_SetPushPullOutput(LE_GPIOPIN21_ACTIVE_LOW, true);
//     LE_INFO("Pin21 read PP - Low: %d", le_gpioPin21_Read());

//     LE_INFO("Pin 21 is input (3) = %s", le_gpioPin21_IsInput()?"true":"false");
//     LE_INFO("Pin 21 is output (3)= %s", le_gpioPin21_IsOutput()?"true":"false");
//     pol = le_gpioPin21_GetPolarity();
//     if (pol == LE_GPIOPIN21_ACTIVE_HIGH)
//     {
//         LE_INFO("Pin 21 polarity (2) = ACTIVE_HIGH");
//     }
//     else
//     {
//         LE_INFO("Pin 21 polarity (2) = ACTIVE_LOW");
//     }
//     le_gpioPin21_SetPushPullOutput(LE_GPIOPIN21_ACTIVE_HIGH, false);
//     LE_INFO("Pin21 read PP - High: %d", le_gpioPin21_Read());

//     pol = le_gpioPin21_GetPolarity();
//     if (pol == LE_GPIOPIN21_ACTIVE_HIGH)
//     {
//         LE_INFO("Pin 21 polarity (3) = ACTIVE_HIGH");
//     }
//     else
//     {
//         LE_INFO("Pin 21 polarity (3)= ACTIVE_LOW");
//     }
//     le_gpioPin21_SetPushPullOutput(LE_GPIOPIN21_ACTIVE_LOW, false);
//     LE_INFO("Pin21 read PP - Low: %d", le_gpioPin21_Read());
// }

//-------------------------------------------------------------------------------------------------
/**
 *  Pin-per-service GPIO pin22 as example
 */
// -------------------------------------------------------------------------------------------------
static void Pin22GpioSignal()
{
    // bool value = false;
    // le_gpioPin22_SetInput(LE_GPIOPIN22_ACTIVE_LOW);
    // value = le_gpioPin22_Read();
    // LE_INFO("Pin22 read active: %d", value);

    // le_gpioPin22_ChangeEventHandlerRef_t ref = le_gpioPin22_AddChangeEventHandler(LE_GPIOPIN22_EDGE_FALLING, Pin22ChangeCallback, &Pin22, 0);

    // // Change the edge setting
    // le_gpioPin22_SetEdgeSense(LE_GPIOPIN22_EDGE_BOTH);
    // le_gpioPin22_DisableEdgeSense();
    // le_gpioPin22_SetEdgeSense(LE_GPIOPIN22_EDGE_RISING);

    // // Remove the handler
    // le_gpioPin22_RemoveChangeEventHandler(ref);

    LE_INFO("My test : PIN 22");
    LE_INFO("===================== GET POLARITY MODE =====================");

    le_gpioPin22_Polarity_t get_polarity = le_gpioPin22_GetPolarity();
    if (get_polarity == LE_GPIOPIN22_ACTIVE_HIGH)
    {
        LE_INFO("polariy high");
    }
    else if (get_polarity == LE_GPIOPIN22_ACTIVE_LOW)
    {
        LE_INFO("polarity low");
    }
     
    LE_INFO("Is output = %s", le_gpioPin22_IsOutput() ? "yes" : "no"); 
    LE_INFO("Is input = %s", le_gpioPin22_IsInput() ? "yes" : "no");

    // set output 
    LE_INFO("======================= Set output ================");
    le_gpioPin22_SetPushPullOutput(LE_GPIOPIN22_ACTIVE_HIGH, 0);
    LE_INFO("Is output = %s", le_gpioPin22_IsOutput() ? "yes" : "no"); 
    LE_INFO("Is input = %s", le_gpioPin22_IsInput() ? "yes" : "no");

    get_polarity = le_gpioPin22_GetPolarity();
    if (get_polarity == LE_GPIOPIN22_ACTIVE_HIGH)
    {
        LE_INFO("polariy high");
    }
    else if (get_polarity == LE_GPIOPIN22_ACTIVE_LOW)
    {
        LE_INFO("polarity low");
    }
    le_gpioPin22_Activate();
    LE_INFO("Is active = %s", le_gpioPin22_IsActive() ? "yes" : "no"); 
    
    le_gpioPin22_SetPushPullOutput(LE_GPIOPIN22_ACTIVE_LOW, 0);
    LE_INFO("Is output = %s", le_gpioPin22_IsOutput() ? "yes" : "no"); 
    LE_INFO("Is input = %s", le_gpioPin22_IsInput() ? "yes" : "no");

    get_polarity = le_gpioPin22_GetPolarity();
    if (get_polarity == LE_GPIOPIN22_ACTIVE_HIGH)
    {
        LE_INFO("polariy high");
    }
    else if (get_polarity == LE_GPIOPIN22_ACTIVE_LOW)
    {
        LE_INFO("polarity low");
    }

    LE_INFO("read = %s", le_gpioPin22_Read() ? "active" : "no");



    LE_INFO("===================== INPUT MODE =====================");
    le_gpioPin22_SetInput(LE_GPIOPIN22_ACTIVE_HIGH);
    LE_INFO("Is output = %s", le_gpioPin22_IsOutput() ? "yes" : "no"); 
    LE_INFO("Is input = %s", le_gpioPin22_IsInput() ? "yes" : "no");

    get_polarity = le_gpioPin22_GetPolarity();
    if (get_polarity == LE_GPIOPIN22_ACTIVE_HIGH)
    {
        LE_INFO("polariy high");
    }
    else if (get_polarity == LE_GPIOPIN22_ACTIVE_LOW)
    {
        LE_INFO("polarity low");
    }

    le_gpioPin22_SetInput(LE_GPIOPIN22_ACTIVE_LOW);
    LE_INFO("Is output = %s", le_gpioPin22_IsOutput() ? "yes" : "no"); 
    LE_INFO("Is input = %s", le_gpioPin22_IsInput() ? "yes" : "no");
    
    get_polarity = le_gpioPin22_GetPolarity();
    if (get_polarity == LE_GPIOPIN22_ACTIVE_HIGH)
    {
        LE_INFO("polariy high");
    }
    else if (get_polarity == LE_GPIOPIN22_ACTIVE_LOW)
    {
        LE_INFO("polarity low");
    }
    
    le_gpioPin22_ChangeEventHandlerRef_t rt = le_gpioPin22_AddChangeEventHandler(LE_GPIOPIN22_EDGE_FALLING, Pin22ChangeCallback, &Pin22, 0);
le_gpioPin22_RemoveChangeEventHandler(rt);
   // LE_INFO("===================== GET POLARITY MODE =====================");
   // LE_INFO("Is active %s", le_gpioPin22_IsActive() ? "yes" : "no");
    
    // set 

}

// static void Pin23GpioSignal(){
//     LE_INFO("My test : PIN 23");
//     LE_INFO("===================== GET POLARITY MODE =====================");

//     le_gpioPin23_Polarity_t get_polarity = le_gpioPin23_GetPolarity();
//     if (get_polarity == LE_GPIOPIN23_ACTIVE_HIGH)
//     {
//         LE_INFO("polariy high");
//     }
//     else if (get_polarity == LE_GPIOPIN23_ACTIVE_LOW)
//     {
//         LE_INFO("polarity low");
//     }
    

// }

// static void PinsReadConfig()
// {
//     LE_INFO("Pin 21 active = %s", le_gpioPin21_IsActive()?"true":"false");

//     le_gpioPin22_Edge_t edge = le_gpioPin22_GetEdgeSense();
//     if (edge == LE_GPIOPIN22_EDGE_FALLING)
//     {
//         LE_INFO("Pin 22 edge sense = falling");
//     }
//     else if (edge == LE_GPIOPIN22_EDGE_RISING)
//     {
//         LE_INFO("Pin 22 edge sense = rising");
//     }
//     else if (edge == LE_GPIOPIN22_EDGE_BOTH)
//     {
//         LE_INFO("Pin 22 edge sense = both");
//     }
//     else if (edge == LE_GPIOPIN22_EDGE_NONE)
//     {
//         LE_INFO("Pin 22 edge sense = none");
//     }

//     le_gpioPin21_Polarity_t pol = le_gpioPin21_GetPolarity();
//     if (pol == LE_GPIOPIN21_ACTIVE_HIGH)
//     {
//         LE_INFO("Pin 21 polarity = ACTIVE_HIGH");
//     }
//     else
//     {
//         LE_INFO("Pin 21 polarity = ACTIVE_LOW");
//     }

//     LE_INFO("Pin 21 is input = %s", le_gpioPin21_IsInput()?"true":"false");
//     LE_INFO("Pin 22 is output = %s", le_gpioPin22_IsOutput()?"true":"false");

//     le_gpioPin21_PullUpDown_t pud = le_gpioPin21_GetPullUpDown();
//     if (pud == LE_GPIOPIN21_PULL_DOWN)
//     {
//         LE_INFO("Pin 21 pull up/down = down");
//     }
//     else if (pud == LE_GPIOPIN21_PULL_UP)
//     {
//         LE_INFO("Pin 21 pull up/down = up");
//     }
//     else
//     {
//         LE_INFO("Pin 21 pull up/down = none");
//     }
// }

// static void SecondCallbackTest()
// {
//     // Try adding this twice
//     le_gpioPin22_AddChangeEventHandler(LE_GPIOPIN22_EDGE_RISING, Pin22ChangeCallback, &Pin22, 0);
//     le_gpioPin22_AddChangeEventHandler(LE_GPIOPIN22_EDGE_FALLING, Pin22ChangeCallback, &Pin22, 0);
// }

COMPONENT_INIT
{
    // LE_INFO("This is sample gpioctl Legato CF3 GPIO app by using le_gpio.api\n");

    // Pin21GpioSignal();

    LE_INFO("\n\n\n========== Pin22GpioSignal ===========\n");
    Pin22GpioSignal();

    // LE_INFO("\n\n\n========== PinsReadConfig ===========\n");
    // PinsReadConfig();

    // // This should abort the test app
    // SecondCallbackTest();

    return;
}
