#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/IRBuilder.h"
#include <map>
#include <vector>

using namespace llvm;

namespace
{
	struct ir_instrumentation : public ModulePass
	{
		static char ID;
		Function *lock, *unlock;

		ir_instrumentation() : ModulePass(ID) {}

		virtual bool runOnModule(Module &M)
		{
			errs() << "====----- Entered Module " << M.getName() << ".\n";

			int counter = -1, counter_unlock = -1;
			std::map<int, Instruction *> workList, workList_unlock;
			for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F)
			{
				if (F->getName() == "uta_mutex_lock_cri")
				{
					errs() << "found lock_cri!"  << "\n";
					lock = cast<Function>(F);
					continue;
				}
				if (F->getName() == "uta_mutex_unlock_cri")
				{
					errs() << "found lock_cri!"   << "\n";
					unlock = cast<Function>(F);
					continue;
				}
			}
			for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F)
			{
				if (F->getName() == "__uta_lock_fifo" || F->getName() == "__uta_mutex_unlock" || F->getName() == "delay_nops")
				{
					continue;
				}

				for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB)
				{
					for (BasicBlock::iterator BI = BB->begin(), BE = BB->end(); BI != BE; ++BI)
					{
						auto *ci = dyn_cast<CallInst>(&(*BI));
						if (isa<CallInst>(&(*BI)))
						{
							Function *fun = ci->getCalledFunction();
							if(fun == NULL){
								errs() << "in function  " << F->getName() << " null\n";
							}
							// errs() << "in function  " << F->getName() << " call\n";
							if (fun->getName() == "uta_mutex_lock")
							{
								auto arg1 = ci->getArgOperand(0);
								auto arg2 = ci->getArgOperand(1);
								counter++;
								std::vector<llvm::Value *> putsArgs;
								putsArgs.push_back(arg1);
								putsArgs.push_back(arg2);
								putsArgs.push_back(ConstantInt::get(Type::getInt32Ty(M.getContext()), counter, true));
								// putsArgs.push_back(ConstantInt::get(Type::getInt32Ty(M.getContext()), 2, true));
								llvm::ArrayRef<llvm::Value *> arguments_lock(putsArgs);
								Instruction *newInst = CallInst::Create(lock, arguments_lock, "");
								BB->getInstList().insert(BI, newInst);
								workList.insert(std::pair<int, Instruction *>(counter, dyn_cast<CallInst>(&(*BI))));
								// BI->eraseFromParent();
								// ReplaceInstWithInst(dyn_cast<CallInst>(&(*BI)), newInst);
							}

							if (fun->getName() == "uta_mutex_unlock")
							{
								errs() << "function  " << F->getName() << " is uta_mutex_unlock call\n";
								auto arg3 = ci->getArgOperand(0);
								auto arg4 = ci->getArgOperand(1);
								counter_unlock++;
								std::vector<llvm::Value *> putsArgs;
								putsArgs.push_back(arg3);
								putsArgs.push_back(arg4);
								// putsArgs.push_back(ConstantInt::get(Type::getInt32Ty(M.getContext()), counter, true));
								// putsArgs.push_back(ConstantInt::get(Type::getInt32Ty(M.getContext()), 2, true));
								llvm::ArrayRef<llvm::Value *> arguments_unlock(putsArgs);
								Instruction *newInst = CallInst::Create(unlock, arguments_unlock, "");
								BB->getInstList().insert(BI, newInst);
								workList_unlock.insert(std::pair<int, Instruction *>(counter_unlock, dyn_cast<CallInst>(&(*BI))));
								// counter++;
								// BI->eraseFromParent();
								// ReplaceInstWithInst(dyn_cast<CallInst>(&(*BI)), newInst);
								errs() << "replace the function!\n";
							}
							// errs() << "in function  " << F->getName() << " call fin\n";
						}
					}
				}
			}
			Instruction *I;
			// // errs() << "here!\n";
			// // Instruction *I = workList[1];
			// // I->eraseFromParent();
			for (int i = 0; i < 999; i++)
			{
				I = workList[i];
				if (I == NULL)
					break;
				I->eraseFromParent();
			}

			for (int i = 0; i < 999; i++)
			{
				I = workList_unlock[i];
				if (I == NULL)
					break;
				I->eraseFromParent();
			}

			return true;
		}
	};
	char ir_instrumentation::ID = 0;
	static RegisterPass<ir_instrumentation> X("uta", "uta: uta Pass");
}