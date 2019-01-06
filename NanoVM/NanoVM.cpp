// NanoVM.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include "NanoVM.h"
#include <inttypes.h>

NanoVM::NanoVM(unsigned char* code, uint64_t size) {
	// Initialize cpu
	memset(&cpu, 0x00, sizeof(cpu));
	// Zero out registers
	memset(cpu.registers, 0x00, sizeof(cpu.registers));
	cpu.codeSize = (PAGE_SIZE * (1 + (size / PAGE_SIZE)));
	cpu.stackSize = PAGE_SIZE;
	// allocate whole memory, code pages, stack, +10 bytes
	// +10 bytes is for instruction fetching which might read more bytes than the instruction size
	// This avoids reading memory out side of the VM
	cpu.codeBase = (unsigned char*) malloc(cpu.stackSize + 10 + cpu.codeSize);
	// Set stack base. Stack grows up instead of down like in x86
	cpu.stackBase = cpu.codeBase + cpu.codeSize - cpu.stackSize;
	// Zero out stack + the last 10 bytes
	memset(cpu.stackBase, 0x00, sizeof(cpu.stackSize) + 10);
	// copy the bytecode to the vm
	memcpy(cpu.codeBase, code, size);
	// Set IP to the beginning of code
	cpu.registers[ip] = reinterpret_cast<uint64_t>(cpu.codeBase);
}

NanoVM::NanoVM(std::string fileName) {
	memset(&cpu, 0x00, sizeof(cpu));
	// Zero out registers
	memset(cpu.registers, 0x00, sizeof(cpu.registers));

	std::streampos size;

	std::ifstream file(fileName, std::ios::in | std::ios::binary | std::ios::ate);
	if (file.is_open())
	{
		size = file.tellg();

		cpu.codeSize = (PAGE_SIZE * (1 + (size / PAGE_SIZE)));
		cpu.stackSize = PAGE_SIZE;

		// allocate whole memory, code pages, stack, +10 bytes
		// +10 bytes is for instruction fetching which might read more bytes than the instruction size
		// This avoids reading memory out side of the VM
		cpu.codeBase = (unsigned char*)malloc(cpu.stackSize + 10 + cpu.codeSize);

		file.seekg(0, std::ios::beg);
		file.read((char*)cpu.codeBase, size);
		file.close();

		// Set stack base. Stack grows up instead of down like in x86
		cpu.stackBase = cpu.codeBase + cpu.codeSize;
		// Zero out stack + the last 10 bytes
		memset(cpu.stackBase, 0x00, sizeof(cpu.stackSize) + 10);
		// Set IP to the beginning of code
		cpu.registers[ip] = 0;
	}
	else std::cout << "Unable to open file";
}
NanoVM::~NanoVM() {
	free(cpu.codeBase);
}

void NanoVM::printStatus() {
	std::cout << "********************\n";
	for (int i = 0; i < 8; i++)
		std::printf("reg%d: %d\n", i, cpu.registers[i]);
	std::printf("SP: %d\n", (cpu.registers[esp]));
	if(cpu.registers[flags] & ZERO_FLAG)
		std::printf("Flags: ZERO\n");
	else if (!cpu.registers[flags])
		std::printf("Flags: --\n");
	else if (cpu.registers[flags] & GREATER_FLAG)
		std::printf("Flags: GREATER\n");
	else if (cpu.registers[flags] & SMALLER_FLAG)
		std::printf("Flags: SMALLER\n");
	std::printf("IP: %d\n", cpu.registers[ip]);
	std::cout << "********************\n";
}

template<class T> inline void NanoVM::push(T value) {
	// Check bounds
	if (sizeof(value) + cpu.registers[esp] >= cpu.stackSize) {
		// No room in stack.
		// Throw error or reallocate more pages
		errorFlag = STACK_ERROR;
		return;
	}
	// push to stack
	*reinterpret_cast<T*>(cpu.stackBase + cpu.registers[esp]) = value;
	// update stack pointer
	cpu.registers[esp] += sizeof(value);
}

template<class T> inline T NanoVM::pop() {
	// Check bounds
	if (reinterpret_cast<void*>(cpu.registers[esp] - sizeof(T)) < cpu.stackBase) {
		// Reached the bottom of stack
		errorFlag = STACK_ERROR;
		return 0;
	}
	// pop value from stack
	T value = *reinterpret_cast<T*>(cpu.stackBase + cpu.registers[esp]);
	// update esp
	cpu.registers[esp] -= sizeof(value);
	return value;
}

bool NanoVM::Run() {
	while (true) {
		Instruction inst;
		if (fetch(inst)) {
			if (inst.opcode == Halt) {
				std::cout << "VM halted!" << std::endl;
				return true;
			}
			if (!execute(inst)) {
				switch (errorFlag) {
				case MEMORY_ACCESS:
					std::cout << "Tried to read/write memory outside of VM!" << std::endl;
					break;
				default:
					std::cout << "Unknown error!" << std::endl;
				}
				return false;
			}
		}
		else {
			std::cout << "Invalid instruction!" << std::endl;
			return false;
		}
	}
}

bool NanoVM::execute(Instruction &inst) {
	// set source and destination addresses
	void *dst, *src;
	dst = (inst.isDstMem) ? reinterpret_cast<void*>(cpu.registers[inst.dstReg]) : reinterpret_cast<void*>(&cpu.registers[inst.dstReg]);
	if (inst.srcType == Type::Reg) {
		src = (inst.isSrcMem) ? reinterpret_cast<void*>(cpu.registers[inst.srcReg]) : reinterpret_cast<void*>(&cpu.registers[inst.srcReg]);
	}
	else {
		src = (inst.isSrcMem) ? reinterpret_cast<void*>(cpu.codeBase + inst.immediate) : reinterpret_cast<void*>(&inst.immediate);
	}
	// Do bounds check
	if ((src != &inst.immediate && src != &cpu.registers[inst.srcReg] && (src < cpu.codeBase || src >= cpu.codeBase + cpu.codeSize + cpu.stackSize)) || (dst != &cpu.registers[inst.dstReg] && (dst < cpu.codeBase || dst > cpu.codeBase + cpu.codeSize + cpu.stackSize))) {
		// Source or destination is out side of VM memory
		errorFlag = MEMORY_ACCESS;
		return false;
	}

	#define MATHOP(INST, OP, SIZE) \
    case INST: {         \
		*reinterpret_cast<SIZE*>(dst) OP *reinterpret_cast<SIZE*>(src); \
		break; \
    }

	//USIZE is unsigned and SIZE is signed type => e.g. uint8_t and int8_t 
	#define BRANCH(USIZE, SIZE) \
	switch(inst.opcode) { \
		MATHOP(Opcodes::Add, +=, SIZE) \
		MATHOP(Opcodes::Mov, =, SIZE) \
		MATHOP(Opcodes::Sub, -=, SIZE) \
		MATHOP(Opcodes::Xor, ^=, SIZE) \
		MATHOP(Opcodes::And, &=, SIZE) \
		MATHOP(Opcodes::Or, |=, SIZE) \
		MATHOP(Opcodes::Sar, >>=, SIZE) \
		MATHOP(Opcodes::Sal, <<=, SIZE) \
		MATHOP(Opcodes::Div, /=, SIZE) \
		MATHOP(Opcodes::Mul, *=, SIZE) \
		MATHOP(Opcodes::Mod, %=, SIZE) \
	case Opcodes::Printi: \
		std::printf("%" PRIu64 "", *reinterpret_cast<USIZE*>(src)); \
		break; \
	case Opcodes::Prints: \
		std::printf("%s", src); \
		break; \
	case Opcodes::Printc: \
		std::printf("%c", *reinterpret_cast<USIZE*>(src)); \
		break; \
	case Opcodes::Inc: \
		*reinterpret_cast<USIZE*>(src) += 1; \
		break; \
	case Opcodes::Dec: \
		*reinterpret_cast<USIZE*>(src) -= 1; \
		break; \
	case Opcodes::Push: \
		push(*reinterpret_cast<USIZE*>(src)); \
		break; \
	case Opcodes::Pop: \
		*reinterpret_cast<USIZE*>(dst) = pop<USIZE>(); \
		break; \
	case Opcodes::Jz: \
		if (cpu.registers[flags] & ZERO_FLAG) { \
			cpu.registers[ip] += *reinterpret_cast<SIZE*>(src); \
			return true; \
		} \
		break; \
	case Opcodes::Jnz: \
		if (!(cpu.registers[flags] & ZERO_FLAG)) { \
			cpu.registers[ip] += *reinterpret_cast<SIZE*>(src); \
			return true; \
		} \
		break; \
	case Opcodes::Jg: \
		if (cpu.registers[flags] & GREATER_FLAG) { \
			cpu.registers[ip] += *reinterpret_cast<SIZE*>(src); \
			return true; \
		} \
		break; \
	case Opcodes::Js: \
		if (cpu.registers[flags] & SMALLER_FLAG) { \
			cpu.registers[ip] += *reinterpret_cast<SIZE*>(src); \
			return true; \
		} \
		break; \
	case Opcodes::Jmp: \
		cpu.registers[ip] += *reinterpret_cast<SIZE*>(src); \
		break; \
	case Opcodes::Call: \
		push(cpu.registers[ip]); \
		cpu.registers[ip] += *reinterpret_cast<SIZE*>(src); \
		break; \
	case Opcodes::Ret: \
		cpu.registers[ip] = *reinterpret_cast<uint64_t*>(pop<uint64_t>()); \
		break; \
	case Opcodes::Cmp: \
		if (*reinterpret_cast<USIZE*>(dst) == *reinterpret_cast<USIZE*>(src)) \
			cpu.registers[flags] = ZERO_FLAG; \
		else if (*reinterpret_cast<USIZE*>(dst) > *reinterpret_cast<USIZE*>(src)) \
			cpu.registers[flags] = GREATER_FLAG; \
		else \
			cpu.registers[flags] = SMALLER_FLAG; \
		break; \
	default: \
		return false; \
	}


	switch (inst.srcSize) {	
	case Size::Byte: 
		BRANCH(uint8_t, int8_t);
		break; 
	case Size::Short: 
		BRANCH(uint16_t, int16_t);
		break; 
	case Size::Dword: 
		BRANCH(uint32_t, int32_t);
		break; 
	default:
		BRANCH(uint64_t, int64_t);
		break; 
	} 
	cpu.registers[ip] += inst.instructionSize;
	return true;
}

bool NanoVM::fetch(Instruction &inst) {
	// Read 64bit to try and minimize the required memory reading
	// This increases the performance

	// Sanity check the ip that it is within code page
	if (cpu.registers[ip] > cpu.codeSize) {
		std::cout << "IP out of bounds" << std::endl;
		return false;
	}
	// Parse the instruction
	unsigned char* rawIp = cpu.codeBase + cpu.registers[ip];
	uint64_t value = *((uint64_t*)(rawIp));
	inst.opcode   =  (value & (unsigned char)OPCODE_MASK);
	inst.dstReg   =  ((value & DST_REG_MASK) >> 5);
	inst.srcType  =  (value >> 8) & SRC_TYPE;
	inst.srcReg   =   (value >> 8) & SRC_REG;
	inst.srcSize  =  ((value >> 8) & SRC_SIZE) >> 5;
	inst.isDstMem =  ((value >> 8) & DST_MEM);
	inst.isSrcMem =  ((value >> 8) & SRC_MEM);
	// If source is immediate value, read it to the instruction struct
	if (inst.srcType) {
		// If the immediate value fit in the initial value. Parse it with bitshift. It is faster than reading memory again
		switch (inst.srcSize) {
		case Byte:
			inst.immediate = (uint8_t)(value >> 16);
			inst.instructionSize = 3;
			break;
		case Short:
			inst.immediate = (uint16_t)(value >> 24);
			inst.instructionSize = 4;
			break;
		case Dword:
			inst.immediate = (uint32_t)(value >> 32);
			inst.instructionSize = 6;
			break;
		case Qword:
			// In the case of qword we have to perform another read operations
			inst.immediate = *(uint64_t*)(rawIp + 2);
			inst.instructionSize = 10;
			break;
		}
	}
	else {
		inst.instructionSize = 2;
	}
	return true;

}

int main(int argc, char* argv[])
{
	if (argc <= 1) {
		std::cout << "Usage NanoVM.exe [FILE]" << std::endl;
		return 0;
	}
	NanoVM vm(argv[1]);
	vm.Run();
	vm.printStatus();
	system("pause");
	return 0;
}