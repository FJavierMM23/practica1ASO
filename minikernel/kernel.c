/*
 *  kernel/kernel.c
 *
 *  Minikernel. Versi�n 1.0
 *
 *  Fernando P�rez Costoya
 *
 */

/*
 *
 * Fichero que contiene la funcionalidad del sistema operativo
 *
 */

#include "kernel.h"	/* Contiene defs. usadas por este modulo */
#include <string.h>

/*
 *
 * Funciones relacionadas con la tabla de procesos:
 *	iniciar_tabla_proc buscar_BCP_libre
 *
 */

/*
 * Funci�n que inicia la tabla de procesos
 */
static void iniciar_tabla_proc(){
	int i, j;

	for (i=0; i<MAX_PROC; i++){
		tabla_procs[i].estado=NO_USADA;
		for (j=0; j<NUM_MUT_PROC; j++)
			tabla_procs[i].desc_mut[j] = -1;
	}
}

/*
 * Funcion que inicia la tabla de mutex
 */
static void iniciar_tabla_mutex(){
	int i;

	for (i=0; i<NUM_MUT; i++){
		tabla_mutex[i].usado = 0;
		tabla_mutex[i].propietario = NULL;
		tabla_mutex[i].bloqueados.primero = NULL;
		tabla_mutex[i].bloqueados.ultimo = NULL;
	}
}

/*
 * Funci�n que busca una entrada libre en la tabla de procesos
 */
static int buscar_BCP_libre(){
	int i;

	for (i=0; i<MAX_PROC; i++)
		if (tabla_procs[i].estado==NO_USADA)
			return i;
	return -1;
}

/*
 *
 * Funciones que facilitan el manejo de las listas de BCPs
 *	insertar_ultimo eliminar_primero eliminar_elem
 *
 * NOTA: PRIMERO SE DEBE LLAMAR A eliminar Y LUEGO A insertar
 */

/*
 * Inserta un BCP al final de la lista.
 */
static void insertar_ultimo(lista_BCPs *lista, BCP * proc){
	if (lista->primero==NULL)
		lista->primero= proc;
	else
		lista->ultimo->siguiente=proc;
	lista->ultimo= proc;
	proc->siguiente=NULL;
}

/*
 * Elimina el primer BCP de la lista.
 */
static void eliminar_primero(lista_BCPs *lista){

	if (lista->ultimo==lista->primero)
		lista->ultimo=NULL;
	lista->primero=lista->primero->siguiente;
}

/*
 * Elimina un determinado BCP de la lista.
 */
static void eliminar_elem(lista_BCPs *lista, BCP * proc){
	BCP *paux=lista->primero;

	if (paux==proc)
		eliminar_primero(lista);
	else {
		for ( ; ((paux) && (paux->siguiente!=proc));
			paux=paux->siguiente);
		if (paux) {
			if (lista->ultimo==paux->siguiente)
				lista->ultimo=paux;
			paux->siguiente=paux->siguiente->siguiente;
		}
	}
}

/*
 *
 * Funciones relacionadas con la planificacion
 *	espera_int planificador
 */

/*
 * Espera a que se produzca una interrupcion
 */
static void espera_int(){
	int nivel;

	printk("-> NO HAY LISTOS. ESPERA INT\n");

	/* Baja al m�nimo el nivel de interrupci�n mientras espera */
	nivel=fijar_nivel_int(NIVEL_1);
	halt();
	fijar_nivel_int(nivel);
}

/*
 * Funci�n de planificacion que implementa un algoritmo FIFO.
 */
static BCP * planificador(){
	while (lista_listos.primero==NULL)
		espera_int();		/* No hay nada que hacer */
	return lista_listos.primero;
}

/* Bloquea al proceso actual en la lista indicada (como sis_dormir) */
static void bloquear_actual(lista_BCPs *lista) {
    int nivel = fijar_nivel_int(NIVEL_3);
    BCP *anterior = p_proc_actual;

    anterior->estado = BLOQUEADO;
    eliminar_primero(&lista_listos);
    insertar_ultimo(lista, anterior);

    p_proc_actual = planificador();
    p_proc_actual->estado = EJECUCION;
    /* con round robin: reponer aquí la rodaja completa */
    cambio_contexto(&(anterior->contexto_regs), &(p_proc_actual->contexto_regs));

    fijar_nivel_int(nivel);
}

/* Pasa un BCP de una lista de bloqueados a listos */
static void desbloquear(BCP *p, lista_BCPs *origen) {
    int nivel = fijar_nivel_int(NIVEL_3);
    eliminar_elem(origen, p);
    p->estado = LISTO;
    insertar_ultimo(&lista_listos, p);
    fijar_nivel_int(nivel);
}

/* Indice en tabla_mutex del mutex con ese nombre, o -1 */
static int buscar_mutex(char *nombre) {
    int i;
    for (i = 0; i < NUM_MUT; i++)
        if (tabla_mutex[i].usado && strcmp(tabla_mutex[i].nombre, nombre) == 0)
            return i;
    return -1;
}

/* Indice de entrada libre en tabla_mutex, o -1 */
static int buscar_mutex_libre() {
    int i;
    for (i = 0; i < NUM_MUT; i++)
        if (!tabla_mutex[i].usado) return i;
    return -1;
}

/* Descriptor libre del proceso actual, o -1 */
static int buscar_desc_libre() {
    int i;
    for (i = 0; i < NUM_MUT_PROC; i++)
        if (p_proc_actual->desc_mut[i] < 0) return i;
    return -1;
}

/* 1 si el descriptor es valido para el proceso actual */
static int desc_valido(unsigned int desc) {
    int idx;
    if (desc >= NUM_MUT_PROC) return 0;
    idx = p_proc_actual->desc_mut[desc];
    if (idx < 0 || idx >= NUM_MUT || !tabla_mutex[idx].usado) return 0;
    return 1;
}

/* Libera el mutex y traspasa la propiedad al primer bloqueado, si lo hay */
static void liberar_propiedad(mutex_t *m) {
    BCP *p = m->bloqueados.primero;
    m->propietario = NULL;
    m->n_locks = 0;
    if (p != NULL) {
        desbloquear(p, &m->bloqueados);
        m->propietario = p;
        m->n_locks = 1;
    }
}

/* Cierra un descriptor del proceso actual (usada por cerrar_mutex
   y por liberar_proceso) */
static void cerrar_mutex_interno(unsigned int desc) {
    int idx = p_proc_actual->desc_mut[desc];
    mutex_t *m = &tabla_mutex[idx];

    /* Si lo tiene bloqueado este proceso, se libera sea cual sea el anidamiento */
    if (m->propietario == p_proc_actual)
        liberar_propiedad(m);

    m->n_abiertos--;
    if (m->n_abiertos == 0) {
        m->usado = 0;
        num_mutex_usados--;
        /* Despertar a todos los que esperan para crear: reevalúan en su while */
        while (lista_esperando_crear.primero != NULL)
            desbloquear(lista_esperando_crear.primero, &lista_esperando_crear);
    }
}

/*
 *
 * Funcion auxiliar que termina proceso actual liberando sus recursos.
 * Usada por llamada terminar_proceso y por rutinas que tratan excepciones
 *
 */
static void liberar_proceso(){
	BCP * p_proc_anterior;

	int i;
	for (i=0; i<NUM_MUT_PROC; i++)
			if (p_proc_actual->desc_mut[i] != -1)
					cerrar_mutex_interno(i);

	liberar_imagen(p_proc_actual->info_mem); /* liberar mapa */

	p_proc_actual->estado=TERMINADO;
	eliminar_primero(&lista_listos); /* proc. fuera de listos */

	/* Realizar cambio de contexto */
	p_proc_anterior=p_proc_actual;
	p_proc_actual=planificador();

	printk("-> C.CONTEXTO POR FIN: de %d a %d\n",
			p_proc_anterior->id, p_proc_actual->id);

	liberar_pila(p_proc_anterior->pila);
	cambio_contexto(NULL, &(p_proc_actual->contexto_regs));
        return; /* no deber�a llegar aqui */
}

/*
 *
 * Funciones relacionadas con el tratamiento de interrupciones
 *	excepciones: exc_arit exc_mem
 *	interrupciones de reloj: int_reloj
 *	interrupciones del terminal: int_terminal
 *	llamadas al sistemas: llam_sis
 *	interrupciones SW: int_sw
 *
 */

/*
 * Tratamiento de excepciones aritmeticas
 */
static void exc_arit(){

	if (!viene_de_modo_usuario())
		panico("excepcion aritmetica cuando estaba dentro del kernel");


	printk("-> EXCEPCION ARITMETICA EN PROC %d\n", p_proc_actual->id);
	liberar_proceso();

        return; /* no deber�a llegar aqui */
}

/*
 * Tratamiento de excepciones en el acceso a memoria
 */
static void exc_mem(){

	if (!viene_de_modo_usuario())
		panico("excepcion de memoria cuando estaba dentro del kernel");


	printk("-> EXCEPCION DE MEMORIA EN PROC %d\n", p_proc_actual->id);
	liberar_proceso();

        return; /* no deber�a llegar aqui */
}

/*
 * Tratamiento de interrupciones de terminal
 */
static void int_terminal(){
	char car;

	car = leer_puerto(DIR_TERMINAL);
	printk("-> TRATANDO INT. DE TERMINAL %c\n", car);

        return;
}

/*
 * Tratamiento de interrupciones de reloj
 */
static void int_reloj(){

	BCP *p = lista_dormidos.primero;
    BCP *sig;

    while (p != NULL){
        sig = p->siguiente;          // guardar ANTES de mover el BCP
        p->ticks--;
        if (p->ticks <= 0){
            p->estado = LISTO;
            eliminar_elem(&lista_dormidos, p);   // 1º sacar de dormidos
            insertar_ultimo(&lista_listos, p);   // 2º meter en listos
        }
        p = sig;
    }
    return;
}

/*
 * Tratamiento de llamadas al sistema
 */
static void tratar_llamsis(){
	int nserv, res;

	nserv=leer_registro(0);
	if (nserv<NSERVICIOS)
		res=(tabla_servicios[nserv].fservicio)();
	else
		res=-1;		/* servicio no existente */
	escribir_registro(0,res);
	return;
}

/*
 * Tratamiento de interrupciuones software
 */
static void int_sw(){

	printk("-> TRATANDO INT. SW\n");

	return;
}

/*
 *
 * Funcion auxiliar que crea un proceso reservando sus recursos.
 * Usada por llamada crear_proceso.
 *
 */
static int crear_tarea(char *prog){
	void * imagen, *pc_inicial;
	int error=0;
	int proc;
	BCP *p_proc;

	proc=buscar_BCP_libre();
	if (proc==-1)
		return -1;	/* no hay entrada libre */

	/* A rellenar el BCP ... */
	p_proc=&(tabla_procs[proc]);

	/* crea la imagen de memoria leyendo ejecutable */
	imagen=crear_imagen(prog, &pc_inicial);
	if (imagen)
	{
		p_proc->info_mem=imagen;
		p_proc->pila=crear_pila(TAM_PILA);
		fijar_contexto_ini(p_proc->info_mem, p_proc->pila, TAM_PILA,
			pc_inicial,
			&(p_proc->contexto_regs));
		p_proc->id=proc;
		p_proc->estado=LISTO;

		/* lo inserta al final de cola de listos */
		insertar_ultimo(&lista_listos, p_proc);
		error= 0;
	}
	else
		error= -1; /* fallo al crear imagen */

	return error;
}

/*
 *
 * Rutinas que llevan a cabo las llamadas al sistema
 *	sis_crear_proceso sis_escribir
 *
 */

/*
 * Tratamiento de llamada al sistema crear_proceso. Llama a la
 * funcion auxiliar crear_tarea sis_terminar_proceso
 */
int sis_crear_proceso(){
	char *prog;
	int res;

	printk("-> PROC %d: CREAR PROCESO\n", p_proc_actual->id);
	prog=(char *)leer_registro(1);
	res=crear_tarea(prog);
	return res;
}

/*
 * Tratamiento de llamada al sistema escribir. Llama simplemente a la
 * funcion de apoyo escribir_ker
 */
int sis_escribir()
{
	char *texto;
	unsigned int longi;

	texto=(char *)leer_registro(1);
	longi=(unsigned int)leer_registro(2);

	escribir_ker(texto, longi);
	return 0;
}

/*
 * Tratamiento de llamada al sistema terminar_proceso. Llama a la
 * funcion auxiliar liberar_proceso
 */
int sis_terminar_proceso(){

	printk("-> FIN PROCESO %d\n", p_proc_actual->id);

	liberar_proceso();

        return 0; /* no deber�a llegar aqui */
}

int sis_obtener_id_pr(){
	return p_proc_actual->id;
}

int sis_dormir(){
	unsigned int segundos = (unsigned int)leer_registro(1);
	int nivel = fijar_nivel_int(NIVEL_3);


	BCP *p_proc_anterior = p_proc_actual;
	p_proc_anterior->estado = BLOQUEADO;
	p_proc_anterior->ticks = segundos * TICK;

	eliminar_primero(&lista_listos);
	insertar_ultimo(&lista_dormidos, p_proc_anterior);

	p_proc_actual = planificador();
	p_proc_actual->estado = EJECUCION;

	cambio_contexto(&(p_proc_anterior->contexto_regs), &(p_proc_actual->contexto_regs));

	fijar_nivel_int(nivel);
	return 0;
}

int sis_crear_mutex() {
    char *nombre = (char *)leer_registro(1);
    int tipo = (int)leer_registro(2);
    int desc, idx;

    if (nombre == NULL || strlen(nombre) >= MAX_NOM_MUT) return -1;
    if (tipo != RECURSIVO && tipo != NO_RECURSIVO) return -1;

    desc = buscar_desc_libre();
    if (desc < 0) return -1;

    while (1) {
        if (buscar_mutex(nombre) >= 0) return -1;
        if (num_mutex_usados < NUM_MUT) break;
        bloquear_actual(&lista_esperando_crear);
    }

    idx = buscar_mutex_libre();
    tabla_mutex[idx] = (mutex_t){
        .tipo = tipo, .usado = 1, .propietario = NULL,
        .n_locks = 0, .n_abiertos = 1, .bloqueados = { NULL, NULL }
    };
    strcpy(tabla_mutex[idx].nombre, nombre);
    num_mutex_usados++;

    p_proc_actual->desc_mut[desc] = idx;
    return desc;
}

int sis_abrir_mutex() {
    char *nombre = (char *)leer_registro(1);
    int desc, idx;

    if (nombre == NULL || strlen(nombre) >= MAX_NOM_MUT) return -1;

    desc = buscar_desc_libre();
    if (desc < 0) return -1;

    idx = buscar_mutex(nombre);
    if (idx < 0) return -1;

    p_proc_actual->desc_mut[desc] = idx;
    tabla_mutex[idx].n_abiertos++;
    return desc;
}

int sis_lock() {
    unsigned int desc = (unsigned int)leer_registro(1);
    mutex_t *m;

    if (!desc_valido(desc)) return -1;
    m = &tabla_mutex[p_proc_actual->desc_mut[desc]];

    if (m->propietario == NULL) {
        m->propietario = p_proc_actual;
        m->n_locks = 1;
        return 0;
    }
    if (m->propietario == p_proc_actual) {
        if (m->tipo == RECURSIVO) { m->n_locks++; return 0; }
        return -1;                      /* no recursivo: interbloqueo trivial */
    }

    bloquear_actual(&m->bloqueados);    /* al despertar, ya somos propietarios */
    return 0;
}

int sis_unlock() {
    unsigned int desc = (unsigned int)leer_registro(1);
    mutex_t *m;

    if (!desc_valido(desc)) return -1;
    m = &tabla_mutex[p_proc_actual->desc_mut[desc]];

    if (m->propietario != p_proc_actual) return -1;

    if (m->tipo == RECURSIVO && m->n_locks > 1) {
        m->n_locks--;
        return 0;
    }
    liberar_propiedad(m);
    return 0;
}

int sis_cerrar_mutex() {
    unsigned int desc = (unsigned int)leer_registro(1);

    if (!desc_valido(desc)) return -1;
    cerrar_mutex_interno(desc);
    p_proc_actual->desc_mut[desc] = -1;
    return 0;
}

/*
 *
 * Rutina de inicializaci�n invocada en arranque
 *
 */
int main(){
	/* se llega con las interrupciones prohibidas */

	instal_man_int(EXC_ARITM, exc_arit); 
	instal_man_int(EXC_MEM, exc_mem); 
	instal_man_int(INT_RELOJ, int_reloj); 
	instal_man_int(INT_TERMINAL, int_terminal); 
	instal_man_int(LLAM_SIS, tratar_llamsis); 
	instal_man_int(INT_SW, int_sw); 

	iniciar_cont_int();		/* inicia cont. interr. */
	iniciar_cont_reloj(TICK);	/* fija frecuencia del reloj */
	iniciar_cont_teclado();		/* inici cont. teclado */

	iniciar_tabla_proc();		/* inicia BCPs de tabla de procesos */
	iniciar_tabla_mutex();		/* inicia tabla de mutex */

	/* crea proceso inicial */
	if (crear_tarea((void *)"init")<0)
		panico("no encontrado el proceso inicial");
	
	/* activa proceso inicial */
	p_proc_actual=planificador();
	cambio_contexto(NULL, &(p_proc_actual->contexto_regs));
	panico("S.O. reactivado inesperadamente");
	return 0;
}
