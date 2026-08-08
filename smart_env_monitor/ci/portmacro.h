/*
 * 语法检查夹具（CI 专用）：GCC 版的 portmacro.h。
 *
 * 注意：这不是烧录进 MCU 的移植层！Keil 工程实际编译链接使用的是
 * Middlewares/Third_Party/FreeRTOS/Source/portable/RVDS/ARM_CM3（armcc
 * 内联汇编），gcc 无法解析，因此 CI 编译门禁（-fsyntax-only）用本文件替代。
 * 本文件为标准 FreeRTOS GCC/ARM_CM3 移植层的等价实现，仅做语法/类型检查，
 * 不参与链接。若升级 FreeRTOS 版本，需同步核对本夹具。
 */
#ifndef PORTMACRO_H
#define PORTMACRO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Type definitions. */
#define portCHAR		char
#define portFLOAT		float
#define portDOUBLE		double
#define portLONG		long
#define portSHORT		short
#define portSTACK_TYPE	uint32_t
#define portBASE_TYPE	long

typedef portSTACK_TYPE StackType_t;
typedef long BaseType_t;
typedef unsigned long UBaseType_t;

#if( configUSE_16_BIT_TICKS == 1 )
	typedef uint16_t TickType_t;
	#define portMAX_DELAY ( TickType_t ) 0xffff
#else
	typedef uint32_t TickType_t;
	#define portMAX_DELAY ( TickType_t ) 0xffffffffUL
#endif
/*-----------------------------------------------------------*/

/* Architecture specifics. */
#define portSTACK_GROWTH			( -1 )
#define portTICK_PERIOD_MS			( ( TickType_t ) 1000 / configTICK_RATE_HZ )
#define portBYTE_ALIGNMENT			8
#define portYIELD()									\
	vPortYieldFromISR()
#define portEND_SWITCHING_ISR( xSwitchRequired )	if( xSwitchRequired != pdFALSE )	vPortYieldFromISR()
#define portYIELD_FROM_ISR( x )						portEND_SWITCHING_ISR( x )
/*-----------------------------------------------------------*/

/* Architecture specific optimisations. */
#ifndef configUSE_PORT_OPTIMISED_TASK_SELECTION
	#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#endif

#if configUSE_PORT_OPTIMISED_TASK_SELECTION == 1
	#define portRECORD_READY_PRIORITY( uxPriority, uxReadyPriorities )	( uxReadyPriorities ) |= ( ( UBaseType_t ) 1 << ( uxPriority ) )
	#define portRESET_READY_PRIORITY( uxPriority, uxReadyPriorities )	( uxReadyPriorities ) &= ~( ( UBaseType_t ) 1 << ( uxPriority ) )
	#define portGET_HIGHEST_PRIORITY( uxTopPriority, uxReadyPriorities )	uxTopPriority = ( 31UL - ( uint32_t ) __builtin_clz( ( uxReadyPriorities ) ) )
	#define portGET_HIGHEST_PRIORITY_INDEX( uxTopPriority, uxReadyPriorities )	uxTopPriority = ( 31UL - ( uint32_t ) __builtin_clz( ( uxReadyPriorities ) ) )
#else
	#define portRECORD_READY_PRIORITY( uxPriority, uxReadyPriorities )	( uxReadyPriorities ) |= ( ( UBaseType_t ) 1 << ( uxPriority ) )
	#define portRESET_READY_PRIORITY( uxPriority, uxReadyPriorities )	( uxReadyPriorities ) &= ~( ( UBaseType_t ) 1 << ( uxPriority ) )
	#define portGET_HIGHEST_PRIORITY( uxTopPriority, uxReadyPriorities )	uxTopPriority = ( 31UL - ( uint32_t ) __builtin_clz( ( uxReadyPriorities ) ) )
	#define portGET_HIGHEST_PRIORITY_INDEX( uxTopPriority, uxReadyPriorities )	uxTopPriority = ( 31UL - ( uint32_t ) __builtin_clz( ( uxReadyPriorities ) ) )
#endif

/*-----------------------------------------------------------*/

/* Critical section management. */
extern void vPortEnterCritical( void );
extern void vPortExitCritical( void );
extern void vPortYieldFromISR( void );

#define portFORCE_INLINE inline __attribute__(( always_inline ))

static portFORCE_INLINE void vPortSetBASEPRI( uint32_t ulBASEPRI )
{
	__asm volatile
	(
		"msr basepri, %0" :: "r" ( ulBASEPRI ) : "memory"
	);
}

static portFORCE_INLINE void vPortRaiseBASEPRI( void )
{
	uint32_t ulNewBASEPRI = configMAX_SYSCALL_INTERRUPT_PRIORITY;

	__asm volatile
	(
		"msr basepri, %0" :: "r" ( ulNewBASEPRI ) : "memory"
	);
}

static portFORCE_INLINE uint32_t ulPortRaiseBASEPRI( void )
{
	uint32_t ulReturn, ulNewBASEPRI = configMAX_SYSCALL_INTERRUPT_PRIORITY;

	__asm volatile
	(
		"mrs %0, basepri\n"
		"msr basepri, %1" : "=r" ( ulReturn ) : "r" ( ulNewBASEPRI ) : "memory"
	);
	return ulReturn;
}

#define portDISABLE_INTERRUPTS()			vPortRaiseBASEPRI()
#define portENABLE_INTERRUPTS()				vPortSetBASEPRI( 0 )
#define portENTER_CRITICAL()				vPortEnterCritical()
#define portEXIT_CRITICAL()					vPortExitCritical()
#define portSET_INTERRUPT_MASK_FROM_ISR()	ulPortRaiseBASEPRI()
#define portCLEAR_INTERRUPT_MASK_FROM_ISR( x )	vPortSetBASEPRI( x )

/*-----------------------------------------------------------*/

/* Task function macros. */
#define portTASK_FUNCTION_PROTO( vFunction, pvParameters )	void vFunction( void *pvParameters )
#define portTASK_FUNCTION( vFunction, pvParameters )	void vFunction( void *pvParameters )

/*-----------------------------------------------------------*/

/* Tickless idle/low power functionality. */
#ifndef portSUPPRESS_TICKS_AND_SLEEP
	#define portSUPPRESS_TICKS_AND_SLEEP( xExpectedIdleTime ) vPortSuppressTicksAndSleep( xExpectedIdleTime )
#endif
extern void vPortSuppressTicksAndSleep( TickType_t xExpectedIdleTime );
/*-----------------------------------------------------------*/

#ifdef configASSERT
	void vPortValidateInterruptPriority( void );
	#define portASSERT_IF_INTERRUPT_PRIORITY_INVALID()	vPortValidateInterruptPriority()
#endif

#define portNOP() __asm volatile ( " nop " )
/*-----------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif /* PORTMACRO_H */
