// Export basic analysis notes for Nokia 2660 loaders
//@category Nokia2660

import java.io.FileWriter;
import java.io.PrintWriter;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.mem.MemoryBlock;

public class ExportLoaderNotes extends GhidraScript {
	@Override
	public void run() throws Exception {
		String outDir = getScriptArgs().length > 0
			? getScriptArgs()[0]
			: "/Users/lovelytingy/Documents/Personal-Projects/Nokia-2660-RE/ghidra/exports";
		String path = outDir + "/" + currentProgram.getName() + "_notes.txt";
		PrintWriter pw = new PrintWriter(new FileWriter(path));
		pw.println("Program: " + currentProgram.getName());
		pw.println("Language: " + currentProgram.getLanguageID());
		pw.println("Image base: " + currentProgram.getImageBase());
		pw.println("Memory:");
		for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
			pw.println("  " + b.getName() + " [" + b.getStart() + " - " + b.getEnd() + "] size=" +
				b.getSize());
		}
		FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
		int n = 0;
		pw.println("Functions (first 100):");
		while (it.hasNext() && n < 100) {
			Function f = it.next();
			pw.println("  " + f.getEntryPoint() + " " + f.getName() + " size=" +
				f.getBody().getNumAddresses());
			n++;
		}
		pw.close();
		println("Wrote " + path);
	}
}
