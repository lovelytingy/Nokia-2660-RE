#ifndef _ELF_H
#define _ELF_H

#include <stdint.h>

#define EI_NIDENT 16
#define ELFMAG "\177ELF"
#define SELFMAG 4

typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Word;
typedef int32_t  Elf32_Sword;
typedef uint32_t Elf32_Addr;
typedef uint32_t Elf32_Off;

typedef struct {
	unsigned char e_ident[EI_NIDENT];
	Elf32_Half    e_type;
	Elf32_Half    e_machine;
	Elf32_Word    e_version;
	Elf32_Addr    e_entry;
	Elf32_Off     e_phoff;
	Elf32_Off     e_shoff;
	Elf32_Word    e_flags;
	Elf32_Half    e_ehsize;
	Elf32_Half    e_phentsize;
	Elf32_Half    e_phnum;
	Elf32_Half    e_shentsize;
	Elf32_Half    e_shnum;
	Elf32_Half    e_shstrndx;
} Elf32_Ehdr;

typedef struct {
	Elf32_Word    sh_name;
	Elf32_Word    sh_type;
	Elf32_Word    sh_flags;
	Elf32_Addr    sh_addr;
	Elf32_Off     sh_offset;
	Elf32_Word    sh_size;
	Elf32_Word    sh_link;
	Elf32_Word    sh_info;
	Elf32_Word    sh_addralign;
	Elf32_Word    sh_entsize;
} Elf32_Shdr;

#define SHT_RELA 4
#define SHT_REL  9

typedef struct {
	Elf32_Addr r_offset;
	Elf32_Word r_info;
} Elf32_Rel;

typedef struct {
	Elf32_Addr  r_offset;
	Elf32_Word  r_info;
	Elf32_Sword r_addend;
} Elf32_Rela;

#define ELF32_R_SYM(val)  ((val) >> 8)
#define ELF32_R_TYPE(val) ((val) & 0xff)

#define R_ARM_RELATIVE 23

#endif /* _ELF_H */
