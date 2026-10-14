/*
 *  minikernel/include/kernel.h
 *
 *  Minikernel. Versi�n 1.0
 *
 *  Fernando P�rez Costoya
 *
 */

/*
 *
 * Fichero de cabecera que contiene definiciones usadas por kernel.c
 *
 *      SE DEBE MODIFICAR PARA INCLUIR NUEVA FUNCIONALIDAD
 *
 */

#ifndef _KERNEL_H
#define _KERNEL_H

#include "const.h"
#include "HAL.h"
#include "llamsis.h"

#define NO_RECURSIVO 0
#define RECURSIVO 1

/*
 *
 * Definicion del tipo que corresponde con el BCP.
 * Se va a modificar al incluir la funcionalidad pedida.
 *
 */
typedef struct BCP_t *BCPptr;

typedef struct BCP_t {
        int id;				/* ident. del proceso */
        int estado;			/* TERMINADO|LISTO|EJECUCION|BLOQUEADO*/
        contexto_t contexto_regs;	/* copia de regs. de UCP */
        void * pila;			/* dir. inicial de la pila */
		int ticks;
		int desc_mut[NUM_MUT_PROC];   /* indice en tabla_mutex, -1 si libre */
	BCPptr siguiente;		/* puntero a otro BCP */
	void *info_mem;			/* descriptor del mapa de memoria */
} BCP;

/*
 *
 * Definicion del tipo que corresponde con la cabecera de una lista
 * de BCPs. Este tipo se puede usar para diversas listas (procesos listos,
 * procesos bloqueados en sem�foro, etc.).
 *
 */

typedef struct{
	BCP *primero;
	BCP *ultimo;
} lista_BCPs;


typedef struct {
    char nombre[MAX_NOM_MUT];
    int tipo;                 // RECURSIVO / NO_RECURSIVO
    int usado;
    BCP *propietario;         // NULL si está libre
    int n_locks;              // nivel de anidamiento (recursivos)
    int n_abiertos;           // cuántos descriptores lo referencian
    lista_BCPs bloqueados;    // procesos esperando en lock
} mutex_t;

/*
 * Variable global que identifica el proceso actual
 */

BCP * p_proc_actual=NULL;

/*
 * Variable global que representa la tabla de procesos
 */

BCP tabla_procs[MAX_PROC];

/*
 * Variable global que representa la cola de procesos listos
 */
lista_BCPs lista_listos= {NULL, NULL};

lista_BCPs lista_dormidos= {NULL, NULL};

mutex_t tabla_mutex[NUM_MUT];

int num_mutex_usados = 0;

lista_BCPs lista_esperando_crear = {NULL, NULL};

/*
 * Round robin
 */

/* Ticks que le quedan de rodaja al proceso en ejecución */
int ticks_rodaja_restantes = TICKS_POR_RODAJA;

/* Proceso al que se pretende expulsar con la interrupción software pendiente.
   NULL si no hay ningún cambio involuntario pendiente */
BCP *p_proc_a_expulsar = NULL;

/*
 *
 * Definici�n del tipo que corresponde con una entrada en la tabla de
 * llamadas al sistema.
 *
 */
typedef struct{
	int (*fservicio)();
} servicio;


/*
 * Prototipos de las rutinas que realizan cada llamada al sistema
 */
int sis_crear_proceso();
int sis_terminar_proceso();
int sis_escribir();
int sis_obtener_id_pr();
int sis_dormir();
int sis_crear_mutex();
int sis_abrir_mutex();
int sis_lock();
int sis_unlock();
int sis_cerrar_mutex();

/*
 * Variable global que contiene las rutinas que realizan cada llamada
 */
servicio tabla_servicios[NSERVICIOS]={	{sis_crear_proceso},
					{sis_terminar_proceso},
					{sis_escribir},
					{sis_obtener_id_pr},
					{sis_dormir},
					{sis_crear_mutex},
					{sis_abrir_mutex},
					{sis_lock},
					{sis_unlock},
					{sis_cerrar_mutex}};

#endif /* _KERNEL_H */

