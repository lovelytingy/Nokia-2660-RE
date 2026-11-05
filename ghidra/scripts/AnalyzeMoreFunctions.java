// Scan for Thumb function entry points and disassemble/create functions
//@category Nokia2660

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;

public class AnalyzeMoreFunctions extends GhidraScript {
	@Override
	public void run() throws Exception {
		Listing listing = currentProgram.getListing();
		Address base = currentProgram.getImageBase();
		Address end = base.add(0xd900);

		// First, disassemble any un-disassembled regions up to 0xd900
		Address cur = base;
		while (cur.compareTo(end) < 0 && !monitor.isCancelled()) {
			if (listing.getInstructionAt(cur) == null && listing.getDataAt(cur) == null) {
				// Try to disassemble
				DisassembleCommand cmd = new DisassembleCommand(cur, null, true);
				cmd.applyTo(currentProgram, monitor);
			}
			cur = cur.add(2);
		}

		// Now scan for function entry points (e.g. push {..., lr})
		cur = base;
		int created = 0;
		while (cur.compareTo(end) < 0 && !monitor.isCancelled()) {
			Instruction inst = listing.getInstructionAt(cur);
			if (inst != null) {
				String mnem = inst.getMnemonicString();
				if (mnem.equalsIgnoreCase("push") && inst.toString().toLowerCase().contains("lr")) {
					if (listing.getFunctionAt(cur) == null) {
						CreateFunctionCmd fnCmd = new CreateFunctionCmd(cur);
						if (fnCmd.applyTo(currentProgram, monitor)) {
							created++;
						}
					}
				}
			}
			cur = cur.add(2);
		}
		println("Created " + created + " new functions.");
	}
}
