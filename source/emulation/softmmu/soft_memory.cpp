/*
 *  Copyright (C) 2016  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include "boxedwine.h"

#ifndef BOXEDWINE_64BIT_MMU
#include "kscheduler.h"
#include "soft_file_map.h"
#include "ksignal.h"
#include "soft_memory.h"
#include "soft_invalid_page.h"
#include "soft_ondemand_page.h"
#include "soft_ro_page.h"
#include "soft_rw_page.h"
#include "soft_wo_page.h"
#include "soft_no_page.h"
#include "soft_copy_on_write_page.h"
#include "soft_code_page.h"
#include "soft_native_page.h"
#include "soft_ram.h"

#include <string.h>
#include <setjmp.h>

//#undef LOG_OPS

extern jmp_buf runBlockJump;

void Memory::log_pf(KThread* thread, U32 address) {
    U32 start = 0;
    U32 i;
    CPU* cpu = thread->cpu;
    KProcess* process = thread->process;

    std::string name = process->getModuleName(cpu->seg[CS].address+cpu->eip.u32);
    printf("%.8X EAX=%.8X ECX=%.8X EDX=%.8X EBX=%.8X ESP=%.8X EBP=%.8X ESI=%.8X EDI=%.8X %s at %.8X\n", cpu->seg[CS].address + cpu->eip.u32, cpu->reg[0].u32, cpu->reg[1].u32, cpu->reg[2].u32, cpu->reg[3].u32, cpu->reg[4].u32, cpu->reg[5].u32, cpu->reg[6].u32, cpu->reg[7].u32, name.c_str(), process->getModuleEip(cpu->seg[CS].address+cpu->eip.u32));

    printf("Page Fault at %.8X\n", address);
    printf("Valid address ranges:\n");
    for (i=0;i<NUMBER_OF_PAGES;i++) {
        if (!start) {
            if (process->memory->mmu[i] != invalidPage) {
                start = i;
            }
        } else {
            if (process->memory->mmu[i] == invalidPage) {
                printf("    %.8X - %.8X\n", start*PAGE_SIZE, i*PAGE_SIZE);
                start = 0;
            }
        }
    }
    printf("Mapped Files:\n");
    for (auto& n : process->getMappedFiles()) {
        const BoxedPtr<MappedFile>& mappedFile = n.second;
        printf("    %.8X - %.8X %s\n", mappedFile->address, mappedFile->address+(int)mappedFile->len, mappedFile->file->openFile->node->path.c_str());
    }
    cpu->walkStack(cpu->eip.u32, EBP, 2);
    kpanic("pf");
}


U8 readb(U32 address) {
    int index = address >> 12;
#ifdef LOG_OPS
    KThread* thread = KThread::currentThread();
    Memory* memory = thread->memory;
    U8 result = thread->memory->mmu[index]->readb(address);;
    if (memory->log && thread->cpu->log)
        fprintf(logFile, "readb %X @%X\n", result, address);
    return result;
#else
    return KThread::currentThread()->memory->mmu[index]->readb(address);
#endif
}

void writeb(U32 address, U8 value) {
    int index = address >> 12;
#ifdef LOG_OPS
    if (memory->log && thread->cpu->log)
        fprintf(logFile, "writeb %X @%X\n", value, address);
#endif
    KThread::currentThread()->memory->mmu[index]->writeb(address, value);
}

U16 readw(U32 address) {
#ifdef LOG_OPS
    U16 result;
    KThread* thread = KThread::currentThread();
    Memory* memory = thread->memory;

    if((address & 0xFFF) < 0xFFF) {
        result = thread->memory->mmu[index]->readw(address);
    } else {
        result = readb(address) | (readb(address+1) << 8);
    }
    if (memory->log && thread->cpu->log)
        fprintf(logFile, "readw %X @%X\n", result, address);
    return result;
#else
    if ((address & 0xFFF) < 0xFFF) {
        int index = address >> 12;
        return KThread::currentThread()->memory->mmu[index]->readw(address);
    }
    return readb(address) | (readb(address+1) << 8);
#endif
}

void writew(U32 address, U16 value) {
#ifdef LOG_OPS
    KThread* thread = KThread::currentThread();
    Memory* memory = thread->memory;
    if (memory->log && thread->cpu->log)
        fprintf(logFile, "writew %X @%X\n", value, address);
#endif
    if ((address & 0xFFF) < 0xFFF) {
        int index = address >> 12;
        KThread::currentThread()->memory->mmu[index]->writew(address, value);
    } else {
        writeb(address, (U8)value);
        writeb(address+1, (U8)(value >> 8));
    }
}

U32 readd(U32 address) {
#ifdef LOG_OPS
    U32 result;
    KThread* thread = KThread::currentThread();
    Memory* memory = thread->memory;

    if ((address & 0xFFF) < 0xFFD) {
        int index = address >> 12;
        result = thread->memory->mmu[index]->readd(address);
    } else {
        result = readb(address) | (readb(address+1) << 8) | (readb(address+2) << 16) | (readb(address+3) << 24);
    }
    if (memory->log && thread->cpu->log)
        fprintf(logFile, "readd %X @%X\n", result, address);
    return result;
#else
    if ((address & 0xFFF) < 0xFFD) {
        int index = address >> 12;
        return KThread::currentThread()->memory->mmu[index]->readd(address);
    } else {
        return readb(address) | (readb(address+1) << 8) | (readb(address+2) << 16) | (readb(address+3) << 24);
    }
#endif
}

void writed(U32 address, U32 value) {
#ifdef LOG_OPS
    KThread* thread = KThread::currentThread();
    Memory* memory = thread->memory;
    if (memory->log && thread->cpu->log)
        fprintf(logFile, "writed %X @%X\n", value, address);
#endif    
    if ((address & 0xFFF) < 0xFFD) {
        int index = address >> 12;
        KThread::currentThread()->memory->mmu[index]->writed(address, value);		
    } else {
        writeb(address, value);
        writeb(address+1, value >> 8);
        writeb(address+2, value >> 16);
        writeb(address+3, value >> 24);
    }
}

U8* Memory::callbackRam;
U32 Memory::callbackRamPos;

void Memory::addCallback(OpCallback func) {
    U64 funcAddress = (U64)func;
    U8* address = callbackRam+callbackRamPos;
    
    *address=0xFE;
    address++;
    *address=0x38;
    address++;
    *address=(U8)funcAddress;
    address++;
    *address=(U8)(funcAddress >> 8);
    address++;
    *address=(U8)(funcAddress >> 16);
    address++;
    *address=(U8)(funcAddress >> 24);
    callbackRamPos+=6;
    if (sizeof(func)==8) {
        address++;
        *address=(U8)(funcAddress >> 32);
        address++;
        *address=(U8)(funcAddress >> 40);
        address++;
        *address=(U8)(funcAddress >> 48);
        address++;
        *address=(U8)(funcAddress >> 56);
        callbackRamPos+=4;
    }
}

class CallbackPage : public NativePage {
    protected:
    CallbackPage(U8* nativeAddress, U32 address, U32 flags) : NativePage(nativeAddress, address, flags) {
    }
public:
    static CallbackPage* CallbackPage::alloc(U8* page, U32 address, U32 flags) {
        return new CallbackPage(page, address, flags);
    }
    void close() {}
};

Memory::Memory(KProcess* process) : process(process) {
    for (int i=0;i<NUMBER_OF_PAGES;i++) {
        this->mmu[i] = invalidPage;
    }

    if (!callbackRam) {
        callbackRam = ramPageAlloc();
        addCallback(onExitSignal);
    }

    this->mmu[CALL_BACK_ADDRESS>>PAGE_SHIFT] = NativePage::alloc(callbackRam, CALL_BACK_ADDRESS, PAGE_READ|PAGE_EXEC);
}

Memory::~Memory() {
    for (int i=0;i<NUMBER_OF_PAGES;i++) {
        this->mmu[i]->close();
    }
}

void Memory::reset() {
    for (int i=0;i<NUMBER_OF_PAGES;i++) {
        this->mmu[i]->close();
        this->mmu[i] = invalidPage;
    }
    this->mmu[CALL_BACK_ADDRESS>>PAGE_SHIFT] = NativePage::alloc(callbackRam, CALL_BACK_ADDRESS, PAGE_READ|PAGE_EXEC);
}

void Memory::reset(U32 page, U32 pageCount) {
    for (U32 i=page;i<page+pageCount;i++) {
        this->mmu[i]->close();
        this->mmu[i] = invalidPage;
    }
}

void Memory::clone(Memory* from) {
    for (int i=0;i<0x100000;i++) {
        Page* page = from->mmu[i];

        this->mmu[i]->close();
        if (page->type == Page::Type::On_Demand_Page) {
            if (page->mapShared()) {
                OnDemandPage*  p = (OnDemandPage*)from->mmu[i];
                p->ondemmand(i<<PAGE_SHIFT);
                // fall through
            } else {                
                this->mmu[i] = OnDemandPage::alloc(page->flags);
                continue;
            }
        }
        if (page->type == Page::Type::File_Page) {
            FilePage* p = (FilePage*)from->mmu[i];
            if (page->mapShared()) {                
                p->ondemmandFile(i<<PAGE_SHIFT);
                // fall through
            } else {
                this->mmu[i] = FilePage::alloc(p->mapped, p->index, p->flags);
                continue;
            }
        }
        page = from->mmu[i]; // above code could have changed this
        if (page->type == Page::Type::RO_Page || page->type == Page::Type::RW_Page || page->type == Page::Type::WO_Page || page->type == Page::Type::NO_Page || page->type == Page::Type::Code_Page) {
            RWPage* p = (RWPage*)from->mmu[i];
            if (!page->mapShared()) {
                if (page->type == Page::Type::WO_Page) {
                    U8* ram = ramPageAlloc();
                    memcpy(ram, p->page, PAGE_SIZE);
                    this->mmu[i] = WOPage::alloc(ram, p->address, p->flags);
                } else if (page->type == Page::Type::NO_Page) {
                    U8* ram = ramPageAlloc();
                    memcpy(ram, p->page, PAGE_SIZE);
                    this->mmu[i] = NOPage::alloc(ram, p->address, p->flags);
                } else {
                    this->mmu[i] = CopyOnWritePage::alloc(p->page, p->address, p->flags);                    
                    from->mmu[i] = CopyOnWritePage::alloc(p->page, p->address, p->flags);
                    p->close();
                }                
            } else {
                if (page->type == Page::Type::RO_Page) {
                    this->mmu[i] = ROPage::alloc(p->page, p->address, p->flags);
                } else if (page->type == Page::Type::RW_Page) {
                    this->mmu[i] = RWPage::alloc(p->page, p->address, p->flags);
                } else if (page->type == Page::Type::WO_Page) {
                    this->mmu[i] = WOPage::alloc(p->page, p->address, p->flags);
                } else if (page->type == Page::Type::NO_Page) {
                    this->mmu[i] = NOPage::alloc(p->page, p->address, p->flags);
                } else if (page->type == Page::Type::Code_Page) {
                    this->mmu[i] = CodePage::alloc(p->page, p->address, p->flags);
                }
            }
        } else if (page->type == Page::Type::Copy_On_Write_Page) {
            CopyOnWritePage* p = (CopyOnWritePage*)from->mmu[i];
            this->mmu[i] = CopyOnWritePage::alloc(p->page, p->address, p->flags);
        } else if (page->type == Page::Type::Native_Page) {
            NativePage* p = (NativePage*)from->mmu[i];
            this->mmu[i] = NativePage::alloc(p->nativeAddress, p->address, p->flags);
        } else if (page->type == Page::Type::Invalid_Page) { 
            this->mmu[i] = from->mmu[i];
        } else {
            kpanic("unhandled case when cloning memory: page type = %d", page->type);
        }
    }
}

void zeroMemory(U32 address, int len) {
    for (int i=0;i<len;i++) {
        writeb(address, 0);
        address++;
    }
}

void readMemory(U8* data, U32 address, int len) {
    for (int i=0;i<len;i++) {
        *data=readb(address);
        address++;
        data++;
    }
}

void writeMemory(U32 address, U8* data, int len) {
    for (int i=0;i<len;i++) {
        writeb(address, *data);
        address++;
        data++;
    }
}

void Memory::allocPages(U32 page, U32 pageCount, U8 permissions, U32 fd, U64 offset, const BoxedPtr<MappedFile>& mappedFile) {
    KThread* thread = KThread::currentThread();

    if (mappedFile) {
        U32 filePage = (U32)(offset>>PAGE_SHIFT);

        if (offset & PAGE_MASK) {
            kpanic("mmap: wasn't expecting the offset to be in the middle of a page");
        }    

        for (U32 i=0;i<pageCount;i++) {
            this->mmu[page+i]->close();
            this->mmu[page+i] = FilePage::alloc(mappedFile, filePage++, permissions);
        }
    } else {
        for (U32 i=0;i<pageCount;i++) {
            this->mmu[page+i]->close();
            this->mmu[page+i] = OnDemandPage::alloc(permissions);
        }
    }    
}

void Memory::protectPage(U32 i, U32 permissions) {
    KThread* thread = KThread::currentThread();
    Page* page = this->mmu[i];

    U32 flags = page->flags;
    flags&=~PAGE_PERMISSION_MASK;
    flags|=(permissions & PAGE_PERMISSION_MASK);

    if (page->type == Page::Type::Invalid_Page) {
        this->mmu[i] = OnDemandPage::alloc(flags);
    } else if (page->type == Page::Type::RO_Page || page->type == Page::Type::RW_Page || page->type == Page::Type::WO_Page || page->type == Page::Type::NO_Page) {
        RWPage* p = (RWPage*)page;

        if ((permissions & PAGE_READ) && (permissions & PAGE_WRITE)) {
            if (page->type != Page::Type::RW_Page) {
                this->mmu[i] = RWPage::alloc(p->page, p->address, flags);
                page->close();
            }
        } else if (permissions & PAGE_WRITE) {
            if (page->type != Page::Type::WO_Page) {
                this->mmu[i] = WOPage::alloc(p->page, p->address, flags);
                page->close();
            }
        } else if (permissions & PAGE_READ) {
            if (page->type != Page::Type::RO_Page) {
                this->mmu[i] = ROPage::alloc(p->page, p->address, flags);
                page->close();
            }
        } else {
            if (page->type != Page::Type::NO_Page) {
                this->mmu[i] = NOPage::alloc(p->page, p->address, flags);
                page->close();
            }
        }
    } else if (page->type == Page::Type::Copy_On_Write_Page) {
        if ((permissions & PAGE_READ) && (permissions & PAGE_WRITE)) {
            page->flags = flags;
        } else if (permissions & PAGE_WRITE) {
            CopyOnWritePage* p = (CopyOnWritePage*)this->mmu[i];
            U8* ram = ramPageAlloc();
            memcpy(ram, p->page, PAGE_SIZE);
            this->mmu[i] = WOPage::alloc(ram, p->address, flags);            
            page->close();
        } else if (permissions & PAGE_READ) {
            page->flags = flags;
        } else {
            CopyOnWritePage* p = (CopyOnWritePage*)this->mmu[i];
            U8* ram = ramPageAlloc();
            memcpy(ram, p->page, PAGE_SIZE);
            this->mmu[i] = NOPage::alloc(ram, p->address, flags);
            page->close();
        }
    } else if (page->type == Page::Type::File_Page || page->type == Page::Type::On_Demand_Page) {
        page->flags = flags;
    } else if (page->type == Page::Type::Code_Page) {
        if (!(permissions & PAGE_READ)) {
            kwarn("Memory::protect removing read flag from code page is not handled");
        }
        page->flags = flags;
    } else {
        kwarn("Memory::protect didn't expect page type: %d", page->type);
    }
}

bool Memory::findFirstAvailablePage(U32 startingPage, U32 pageCount, U32* result, bool canBeReMapped) {
    U32 i;
    
    for (i=startingPage;i<NUMBER_OF_PAGES;i++) {
        if (this->mmu[i]->type==Page::Type::Invalid_Page || (canBeReMapped && (this->mmu[i]->flags & PAGE_MAPPED))) {
            U32 j;
            bool success = true;

            for (j=1;j<pageCount;j++) {
                U32 nextPage = i+j; // could be done a different way, but this helps the static analysis
                if (nextPage < NUMBER_OF_PAGES && this->mmu[nextPage]->type!=Page::Type::Invalid_Page && (!canBeReMapped || !(this->mmu[i]->flags & PAGE_MAPPED))) {
                    success = false;
                    break;
                }
            }
            if (success) {
                *result = i;
                return true;
            }
            i+=j; // no reason to check all the pages again
        }
    }
    return false;
}

bool Memory::isValidReadAddress(U32 address, U32 len) {
    U32 startPage = address>>PAGE_SHIFT;
    U32 endPage = (address+len-1)>>PAGE_SHIFT;
    for (U32 i=startPage;i<=endPage;i++) {
        if (!this->mmu[address >> PAGE_SHIFT]->canRead())
            return false;
    }
    return true;
}

bool Memory::isValidWriteAddress(U32 address, U32 len) {
    U32 startPage = address>>PAGE_SHIFT;
    U32 endPage = (address+len-1)>>PAGE_SHIFT;
    for (U32 i=startPage;i<=endPage;i++) {
        if (!this->mmu[address >> PAGE_SHIFT]->canWrite())
            return false;
    }
    return true;
}

bool Memory::isPageAllocated(U32 page) {
    return this->mmu[page]->type!=Page::Type::Invalid_Page;
}

U8* getPhysicalAddress(U32 address) {
    int index = address >> 12;
    return KThread::currentThread()->process->memory->mmu[index]->physicalAddress(address);
}

void memcopyFromNative(U32 address, const char* p, U32 len) {
#ifdef UNALIGNED_MEMORY
    U32 i;
    for (i=0;i<len;i++) {
        writeb(thread, address+i, p[i]);
    }
#else
    U32 i;

    if (len>4) {
        U8* ram = getPhysicalAddress(address);
    
        if (ram) {
            U32 todo = PAGE_SIZE-(address & (PAGE_SIZE-1));
            if (todo>len)
                todo=len;
            while (1) {
                memcpy(ram, p, todo);
                len-=todo;
                if (!len) {
                    return;
                }
                address+=todo;
                p+=todo;
                ram = getPhysicalAddress(address);
                if (!ram) {
                    break;
                }
                todo = PAGE_SIZE;
                if (todo>len)
                    todo=len;
            }
        }
    }

    for (i=0;i<len;i++) {
        writeb(address+i, p[i]);
    }
#endif
}

void memcopyToNative(U32 address, char* p, U32 len) {
#ifdef UNALIGNED_MEMORY
    for (U32 i=0;i<len;i++) {
        p[i] = readb(thread, address+i);
    }
#else
    if (len>4) {
        U8* ram = getPhysicalAddress(address);
    
        if (ram) {
            U32 todo = PAGE_SIZE-(address & (PAGE_SIZE-1));
            if (todo>len)
                todo=len;
            while (1) {
                memcpy(p, ram, todo);
                len-=todo;
                if (!len) {
                    return;
                }
                address+=todo;
                p+=todo;
                ram = getPhysicalAddress(address);
                if (!ram) {
                    break;
                }
                todo = PAGE_SIZE;
                if (todo>len)
                    todo=len;
            }
        }
    }
    
    for (U32 i=0;i<len;i++) {
        p[i] = readb(address+i);
    }
#endif
}

void writeNativeString(U32 address, const char* str) {	
    while (*str) {
        writeb(address, *str);
        str++;
        address++;
    }
    writeb(address, 0);
}

U32 writeNativeString2(U32 address, const char* str, U32 len) {	
    U32 count=0;

    while (*str && count<len-1) {
        writeb(address, *str);
        str++;
        address++;
        count++;
    }
    writeb(address, 0);
    return count;
}

void writeNativeStringW(U32 address, const char* str) {	
    while (*str) {
        writew(address, *str);
        str++;
        address+=2;
    }
    writew(address, 0);
}

char* getNativeString(U32 address, char* buffer, U32 cbBuffer) {
    char c;
    U32 i=0;

    if (!address) {
        buffer[0]=0;
        return buffer;
    }
    do {
        c = readb(address++);
        buffer[i++] = c;
    } while(c && i<cbBuffer);
    buffer[cbBuffer-1]=0;
    return buffer;
}

U32 getNativeStringLen(U32 address) {
    char c;
    U32 i=0;

    if (!address) {
        return 0;
    }
    do {
        c = readb(address++);
        i++;
    } while(c);
    return i;
}

char* getNativeStringW(U32 address, char* buffer, U32 cbBuffer) {
    char c;
    U32 i=0;

    if (!address) {
        buffer[0]=0;
        return buffer;
    }
    do {
        c = (char)readw(address);
        address+=2;
        buffer[i++] = c;
    } while(c && i<cbBuffer);
    buffer[cbBuffer-1]=0;
    return buffer;
}

/*
static U32 callbackPage;
static U8* callbackAddress;
static int callbackPos;
static void* callbacks[512];

void addCallback(void (OPCALL *func)(struct CPU*, struct Op*)) {
    U64 funcAddress = (U64)func;
    U8* address = callbackAddress+callbackPos*(4+sizeof(void*));
    callbacks[callbackPos++] = address;
    
    *address=0xFE;
    address++;
    *address=0x38;
    address++;
    *address=(U8)funcAddress;
    address++;
    *address=(U8)(funcAddress >> 8);
    address++;
    *address=(U8)(funcAddress >> 16);
    address++;
    *address=(U8)(funcAddress >> 24);
    if (sizeof(func)==8) {
        address++;
        *address=(U8)(funcAddress >> 32);
        address++;
        *address=(U8)(funcAddress >> 40);
        address++;
        *address=(U8)(funcAddress >> 48);
        address++;
        *address=(U8)(funcAddress >> 56);
    }
}
*/
void initCallbacksInProcess(KProcess* process) {
    /*
    U32 page = CALL_BACK_ADDRESS >> PAGE_SHIFT;

    process->memory->mmu[page] = &ramPageRO;
    process->memory->flags[page] = PAGE_READ|PAGE_EXEC|PAGE_IN_RAM;
    process->memory->ramPage[page] = callbackPage;
    process->memory->read[page] = TO_TLB(callbackPage,  CALL_BACK_ADDRESS);
    incrementRamRef(callbackPage);
    */
}
/*
void initCallbacks() {
    callbackPage = allocRamPage();
    callbackAddress = getAddressOfRamPage(callbackPage);
    addCallback(onExitSignal);
}

void initBlockCache() {
}

struct Block* getBlock(struct CPU* cpu, U32 eip) {
    struct Block* block;	
    U32 ip;

    if (cpu->big)
        ip = cpu->segAddress[CS] + eip;
    else
        ip = cpu->segAddress[CS] + (eip & 0xFFFF);

    U32 page = ip >> PAGE_SHIFT;
    U32 flags = cpu->memory->flags[page];
    if (IS_PAGE_IN_RAM(flags)) {
        block = getCode(cpu->memory->ramPage[page], ip & 0xFFF);
        if (!block) {
            block = decodeBlock(cpu, eip);
            addCode(block, cpu, ip, block->eipCount);
        }
    } else {		
        block = decodeBlock(cpu, eip);
        addCode(block, cpu, ip, block->eipCount);
    }
    cpu->memory->write[page]=0;
    cpu->memory->mmu[page] = &codePage;
    return block;
}
*/

U32 Memory::mapNativeMemory(void* hostAddress, U32 size) {
    U32 result = 0;

    if (this->nativeAddressStart && hostAddress>=this->nativeAddressStart && (U8*)hostAddress+size<(U8*)this->nativeAddressStart+0x10000000) {
        return (ADDRESS_PROCESS_NATIVE<<PAGE_SHIFT) + ((U8*)hostAddress-(U8*)this->nativeAddressStart);
    }
    if (!this->nativeAddressStart) {
        U32 i;

        // just assume we are in the middle, hopefully OpenGL won't want more than 128MB before or after this initial address
        this->nativeAddressStart = ((U8*)hostAddress - ((U32)hostAddress & 0xFFF)) - 0x08000000;
        for (i=0;i<0x10000;i++) {
            this->mmu[i+ADDRESS_PROCESS_NATIVE] = NativePage::alloc(this->nativeAddressStart+PAGE_SIZE*i, (ADDRESS_PROCESS_NATIVE<<PAGE_SHIFT)+PAGE_SIZE*i, PAGE_READ | PAGE_WRITE);
        }
        return mapNativeMemory(hostAddress, size);
    }
    U32 pageCount = (size+PAGE_MASK)>>PAGE_SHIFT;
    // hopefully this won't happen much, because it will leak address space
    if (!findFirstAvailablePage(0xD0000000, pageCount, &result, false)) {
        kpanic("mapNativeMemory failed to map address: size=%d", size);
    }

    for (U32 i=0;i<pageCount;i++) {
        this->mmu[result+i]->close();
        this->mmu[result+i] = NativePage::alloc((U8*)hostAddress+PAGE_SIZE*i, (ADDRESS_PROCESS_NATIVE<<PAGE_SHIFT)+PAGE_SIZE*i, PAGE_READ | PAGE_WRITE);
    }
    return result<<PAGE_SHIFT;
}

void Memory::map(U32 startPage, const std::vector<U8*>& pages, U32 permissions) {
    U32 result = 0;
    bool read = (permissions & PAGE_READ)!=0 || (permissions && PAGE_EXEC)!=0;
    bool write = (permissions & PAGE_WRITE)!=0;

    for (U32 page=0;page<pages.size();page++) {
        this->mmu[startPage+page]->close();
        if (read && write) {
            this->mmu[startPage+page] = RWPage::alloc(pages[page], (startPage+page)<<PAGE_SHIFT, permissions);
        } else if (read) {
            this->mmu[startPage+page] = ROPage::alloc(pages[page], (startPage+page)<<PAGE_SHIFT, permissions);
        } else if (write) {
            this->mmu[startPage+page] = WOPage::alloc(pages[page], (startPage+page)<<PAGE_SHIFT, permissions);
        } else {
            this->mmu[startPage+page] = NOPage::alloc(pages[page], (startPage+page)<<PAGE_SHIFT, permissions);
        }
    }
}

#endif
