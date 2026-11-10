/* Retail 0x2535AC registers a file-request completion callback. Its immediate
 * (already completed) branch used to call back while holding the queue mutex.
 * 0x183B70 then takes the read-list mutex, whereas 0x186D00 holds the read-list
 * mutex while submitting another request. Native threads can deadlock there.
 * Copy the completed result under the queue mutex and invoke it after release,
 * just as the normal worker at 0x2530F8 already does. The pending/running path
 * still publishes the callback while locked. Do not touch the entry after
 * unlocking: the callback can retire and free it.
 * Included by recomp_manual.c and the isolated lock-order regression test. */
static void vr_io_register_completion(void)
{
    const uint32_t handle=MEM32(esp+4),callback=MEM32(esp+8);
    const uint32_t queue=guest_call_full(sub_00252B0D,ecx,edx,0x2535bbu,1,&handle);
    guest_call_full(sub_00251C41,queue+0x28,edx,0x2535c8u,0,NULL);

    /* 0x253336 has a private register ABI: ESI=queue, EBX=&request state. */
    const uint32_t saved_esi=esi,saved_ebx=ebx;
    PUSH32(esp,0);
    esi=queue;ebx=esp;
    const uint32_t entry=guest_call_full(sub_00253336,ecx,edx,0x2535d3u,1,&handle);
    const uint32_t state=MEM32(esp);
    esp+=4;esi=saved_esi;ebx=saved_ebx;

    uint32_t args[3]={0,0,0};
    if(state)MEM32(entry+0x1c)=callback;
    else {
        args[0]=MEM32(entry+0xc);
        args[1]=(uint32_t)(int32_t)(int8_t)MEM8(entry+0x11);
        args[2]=MEM32(entry+0x18);
    }
    guest_call_full(sub_00251C4C,queue+0x28,edx,0x2535fau,0,NULL);
    if(!state) {
        recomp_func_t function=recomp_lookup_manual(callback);
        if(!function)function=recomp_lookup(callback);
        if(function)guest_call_full(function,ecx,edx,0x2535f0u,3,args);
        else recomp_icall_fail_log(callback);
    }
    eax=0; /* Retail's final RtlLeaveCriticalSection returns zero. */
    esp+=4; /* cdecl: only pop the return address. */
}
