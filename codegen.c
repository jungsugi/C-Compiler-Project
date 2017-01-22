/*code generator
 * - To generate utimate TM codes from syntax tree and symbol tables.
 */

#include "globals.h"
#include "parse.h"
#include "symtab.h"
#include "codegen.h"

/* stack used for call */
TreeNode* paramStack[SIZE];
int top = 0;

/* stack routines*/
int pushParam(TreeNode* param)
{
    if(top == SIZE) 
      return 1;

    paramStack[top++] = param;
    return 0;
}

TreeNode* popParam()
{
  if(top == 0)
    return NULL;

  return paramStack[--top];
}

/* current location */
int emitLoc = 0 ;

/* Highest TM location emitted so far */
int highEmitLoc = 0;

/* skips "howMany" locations for later backpatch
 * returns the current code position
 */
int emitSkip( int howMany)
{  int i = emitLoc;
   emitLoc += howMany ;
   if (highEmitLoc < emitLoc)  highEmitLoc = emitLoc ;
   return i;
} 

/* backs up to a previously skipped location */
void emitBackup( int loc)
{ if (loc > highEmitLoc) emitComment("BUG in emitBackup");
  emitLoc = loc ;
} 

/* restores the current position to the highest previous position*/
void emitRestore(void)
{ emitLoc = highEmitLoc;}

/* prints a comment line */
void emitComment( char * c )
{ if (TraceCode) fprintf(code,"* %s\n",c);}

/* emits a register-only TM instruction
 * op = the opcode
 * r = target register
 * s = 1st source register
 * t = 2nd source register
 * c = a comment to be printed if TraceCode is TRUE
 */
void emitRO( char *op, int r, int s, int t, char *c)
{ fprintf(code,"%3d:  %5s  %d,%d,%d ",emitLoc++,op,r,s,t);
  if (TraceCode) fprintf(code,"\t%s",c) ;
  fprintf(code,"\n") ;
  if (highEmitLoc < emitLoc) highEmitLoc = emitLoc ;
} 

/* emits a register-to-memory TM instruction
 * op = the opcode
 * r = target register
 * d = the offset
 * s = the base register
 * c = a comment to be printed if TraceCode is TRUE
 */
void emitRM( char * op, int r, int d, int s, char *c)
{ fprintf(code,"%3d:  %5s  %d,%d(%d) ",emitLoc++,op,r,d,s);
  if (TraceCode) fprintf(code,"\t%s",c) ;
  fprintf(code,"\n") ;
  if (highEmitLoc < emitLoc)  highEmitLoc = emitLoc ;
} 

/*******************************/

/* generate standard prelude */
void emitPrelude()
{
   if (TraceCode) emitComment("Begin prelude");
   emitRM("LD",gp,0,zero,"load from location 0");
   emitRM("ST",zero,0,zero,"clear location 0");
   emitRM("LDA",sp,-(topTable()->size),gp,"allocate for global variables");
   if (TraceCode) emitComment("End of prelude");
}

/* generate codes for input() */
void emitInput()
{

   if (TraceCode) emitComment("Begin input()");
   FunSymbol* fun = lookup_fun("input");
   fun->offset = emitSkip(0);
   emitRO("IN",ax,0,0,"read input into ax");
   emitRM("LDA",sp,1,sp,"pop prepare");
   emitRM("LD",pc,-1,sp,"pop return addr");
   if (TraceCode) emitComment("End input()");
}

/* generate codes for output() */
void emitOutput()
{

   if (TraceCode) emitComment("Begin output()");
   FunSymbol* fun = lookup_fun("output");
   fun->offset = emitSkip(0);
   emitRM("LD",ax,1,sp,"load param into ax");
   emitRO("OUT",ax,0,0,"output using ax");
   emitRM("LDA",sp,1,sp,"pop prepare");
   emitRM("LD",pc,-1,sp,"pop return addr");
   if (TraceCode) emitComment("End output()");
}
  

/* emit one instruction to get the address of a var,
 * store the address in bx,
 * we can access the var by bx[0]
 */
void emitGetAddr(VarSymbol *var)
{

  switch(var->scope){
    case GLOBAL:
        if(var->type == TYPE_ARRAY){
          emitRM("LDA",bx,-(var->offset),gp,"get global array address");
        }
        else{
          emitRM("LDA",bx,-1-(var->offset),gp,"get global address");
        }
        break;
    case LOCAL:
        if(var->type == TYPE_ARRAY){
          emitRM("LDA",bx,-(var->offset),bp,"get local array address");
        }
        else{
          emitRM("LDA",bx,-1-(var->offset),bp,"get local address");
        }
        break;
    case PARAM:
        if(var->type == TYPE_ARRAY){
          emitRM("LD",bx,2+(var->offset),bp,"get param array address");
        }
        else{
          emitRM("LDA",bx,2+(var->offset),bp,"get param variable address");
        }
        break;
  }
}


/* emits 5 instructions to call a function,
 * before this we have pushed all parameters
 */
void emitCall(FunSymbol *fun)
{

  emitRM("LDA",ax,3,pc,"store returned PC");
  emitRM("LDA",sp,-1,sp,"push prepare");
  emitRM("ST",ax,0,sp,"push returned PC");
  emitRM("LDC",pc,fun->offset,0,"jump to function");
  emitRM("LDA",sp,fun->paramNum,sp,"release parameters");
}


/* getValue:
 * 1 - store value in ax
 * 0 - store address in bx
 */
static int getValue = 1;

/* isRecursive:
 * 1 - cGen will recurse on sibling
 * 0 - cGen won't recurse on sibling
 */
static int isRecursive = 1;

/* recursively generates code by tree traversal */
void cGen( TreeNode * tree)
{ 
  int tmp;
  TreeNode * p1, * p2, * p3;
  int savedLoc1,savedLoc2,currentLoc;

  VarSymbol *var;
  FunSymbol *fun;

    while(tree){
      

        switch (tree->astType) {

          case FUNDEC_AST:
              
              if (TraceCode) emitComment("-> function:");

              p1 = tree->child[0];/*head*/
              p2 = tree->child[1];/*body*/

              fun = lookup_fun(p1->attr.name);
              fun->offset = emitSkip(0);

              
              /*prepare bp & sp*/
              emitRM("LDA",sp,-1,sp,"push prepare");
              emitRM("ST",bp,0,sp,"push old bp");
              emitRM("LDA",bp,0,sp,"let bp == sp");
              emitRM("LDA",sp,-(p2->symbolTable->size),sp,"allocate for local variables");

              /*push param symtab, prepare for body*/
              
              pushTable(fun->symbolTable);
              /*generate body*/
              cGen(p2); 
              popTable();

              /*generate return code for void functions*/
              if(p1->type == TYPE_VOID){
                  /*return*/
                  emitRM("LDA",sp,0,bp,"let sp == bp");
                  emitRM("LDA",sp,2,sp,"pop prepare");
                  emitRM("LD",bp,-2,sp,"pop old bp");
                  emitRM("LD",pc,-1,sp,"pop return addr");
              }

              if (TraceCode) emitComment("<- function");
              
              break;

          case COMPOUND_AST:
             
              if (TraceCode) emitComment("-> compound");
              p1 = tree->child[1];/*statements*/
              
              if(tree->symbolTable)
                pushTable(tree->symbolTable);
              
              cGen(p1);
              
              if(tree->symbolTable)
                popTable();
              if (TraceCode) emitComment("<- compound");
              break;

          case SELESTMT_AST :
             if (TraceCode) emitComment("-> if") ;
             p1 = tree->child[0] ;
             p2 = tree->child[1] ;
             p3 = tree->child[2] ;
             /* generate code for test expression */
             cGen(p1);
             savedLoc1 = emitSkip(1) ;
             emitComment("jump to else ");
             /* recurse on then part */
             cGen(p2);
             savedLoc2 = emitSkip(1) ;
             emitComment("jump to end");
             currentLoc = emitSkip(0) ;
             emitBackup(savedLoc1) ;
             emitRM("JEQ",ax,currentLoc,zero,"if: jmp to else");
             emitRestore() ;
             /* recurse on else part */
             cGen(p3);
             currentLoc = emitSkip(0) ;
             emitBackup(savedLoc2) ;
             emitRM("LDA",pc,currentLoc,zero,"jmp to end") ;
             emitRestore() ;
             if (TraceCode)  emitComment("<- if") ;
             break; 

          case ITERSTMT_AST:
             if (TraceCode) emitComment("-> while") ;
             p1 = tree->child[0] ;
             p2 = tree->child[1] ;
             savedLoc1 = emitSkip(0);
             emitComment("jump here after body");
             /* generate code for test */
             cGen(p1);
             savedLoc2 = emitSkip(1);
             emitComment("jump to end if test fails");
             /* generate code for body */
             cGen(p2);
             emitRM("LDA",pc,savedLoc1,zero,"jump to test");
             currentLoc = emitSkip(0);
             emitBackup(savedLoc2);
             emitRM("JEQ",ax,currentLoc,zero,"jump to end");
             emitRestore();
             if (TraceCode)  emitComment("<- while") ;
             break; 

          case RETSTMT_AST:
            if (TraceCode) emitComment("-> return");
            p1 = tree->child[0];
            /*Only calculate non-voild value*/
            if(tree->type != TYPE_VOID) 
              cGen(p1);

            /*return*/
            emitRM("LDA",sp,0,bp,"let sp == bp");
            emitRM("LDA",sp,2,sp,"pop prepare");
            emitRM("LD",bp,-2,sp,"pop old bp");
            emitRM("LD",pc,-1,sp,"pop return addr");

            if (TraceCode) emitComment("<- return");
            break;


          case NUM_AST:
             if(TraceCode) emitComment("-> number");
             emitRM("LDC",ax,tree->attr.value,0,"store number");
             if(TraceCode) emitComment("<- number");
             break;

          case VAR_AST:
             if(TraceCode) emitComment("-> variable");
             
             var = lookup_var(tree->attr.name);
             emitGetAddr(var);

             if(getValue){
              if(var->type == TYPE_ARRAY)
                emitRM("LDA",ax,0,bx,"get array variable value( == address)");
              else
                emitRM("LD",ax,0,bx,"get variable value");

             }
             if(TraceCode) emitComment("<- variable");
             break;

          case ARRAYVAR_AST:
             if(TraceCode) emitComment("-> array element");
             p1 = tree->child[0];/*index expression*/

             var = lookup_var(tree->attr.name);
             emitGetAddr(var);

             /* protect bx*/
             emitRM("LDA",sp,-1,sp,"push prepare");
             emitRM("ST",bx,0,sp,"protect array address");

             tmp = getValue;
             getValue = 1;
             cGen(p1);
             getValue = tmp;

             /* recover bx*/
             emitRM("LDA",sp,1,sp,"pop prepare");
             emitRM("LD",bx,-1,sp,"recover array address");

             emitRO("SUB",bx,bx,ax,"get address of array element");
             if(getValue)
                emitRM("LD",ax,0,bx,"get value of array element");

             if(TraceCode) emitComment("<- array element");
             break;




          case ASSIGN_AST:
             if (TraceCode) emitComment("-> assign") ;
             p1 = tree->child[0];/*left*/
             p2 = tree->child[1];/*right*/
             /* left value (get its address -> bx)*/
             getValue = 0;
             cGen(p1);
             /* protect bx*/
             emitRM("LDA",sp,-1,sp,"push prepare");
             emitRM("ST",bx,0,sp,"protect bx");
             /* right value -> ax*/
             getValue = 1;
             cGen(p2);
             /* recover bx*/
             emitRM("LDA",sp,1,sp,"pop prepare");
             emitRM("LD",bx,-1,sp,"recover bx");
             /* now we can assign*/
             emitRM("ST",ax,0,bx,"assign: store");
             if (TraceCode)  emitComment("<- assign") ;
             break; 

          case EXP_AST:
             if (TraceCode) emitComment("-> op") ;
             p1 = tree->child[0];/*left*/
             p2 = tree->child[1];/*right*/
             
             cGen(p1);
             /* store left operand */
             emitRM("LDA",sp,-1,sp,"push prepare");
             emitRM("ST",ax,0,sp,"op: protect left");
             
             cGen(p2);
             /* now load left operand */
             emitRM("LDA",sp,1,sp,"pop prepare");
             emitRM("LD",bx,-1,sp,"op: recover left");
             switch (tree->attr.op) {
                case PLUS :
                   emitRO("ADD",ax,bx,ax,"op +");
                   break;
                case MINUS :
                   emitRO("SUB",ax,bx,ax,"op -");
                   break;
                case MULTI :
                   emitRO("MUL",ax,bx,ax,"op *");
                   break;
                case DIV :
                   emitRO("DIV",ax,bx,ax,"op /");
                   break;
                case EQ :
                   emitRO("SUB",ax,bx,ax,"op ==") ;
                   emitRM("JEQ",ax,2,pc,"br if true") ;
                   emitRM("LDC",ax,0,0,"false case") ;
                   emitRM("LDA",pc,1,pc,"unconditional jmp") ;
                   emitRM("LDC",ax,1,0,"true case") ;
                   break;
                case NE :
                   emitRO("SUB",ax,bx,ax,"op !=") ;
                   emitRM("JNE",ax,2,pc,"br if true") ;
                   emitRM("LDC",ax,0,0,"false case") ;
                   emitRM("LDA",pc,1,pc,"unconditional jmp") ;
                   emitRM("LDC",ax,1,0,"true case") ;
                   break;
                case LT :
                   emitRO("SUB",ax,bx,ax,"op <") ;
                   emitRM("JLT",ax,2,pc,"br if true") ;
                   emitRM("LDC",ax,0,0,"false case") ;
                   emitRM("LDA",pc,1,pc,"unconditional jmp") ;
                   emitRM("LDC",ax,1,0,"true case") ;
                   break;
                case GT :
                   emitRO("SUB",ax,bx,ax,"op >") ;
                   emitRM("JGT",ax,2,pc,"br if true") ;
                   emitRM("LDC",ax,0,0,"false case") ;
                   emitRM("LDA",pc,1,pc,"unconditional jmp") ;
                   emitRM("LDC",ax,1,0,"true case") ;
                   break;
                case LE :
                   emitRO("SUB",ax,bx,ax,"op <=") ;
                   emitRM("JLE",ax,2,pc,"br if true") ;
                   emitRM("LDC",ax,0,0,"false case") ;
                   emitRM("LDA",pc,1,pc,"unconditional jmp") ;
                   emitRM("LDC",ax,1,0,"true case") ;
                   break;
                case GE :
                   emitRO("SUB",ax,bx,ax,"op >=") ;
                   emitRM("JGE",ax,2,pc,"br if true") ;
                   emitRM("LDC",ax,0,0,"false case") ;
                   emitRM("LDA",pc,1,pc,"unconditional jmp") ;
                   emitRM("LDC",ax,1,0,"true case") ;
                   break;
                default:
                   emitComment("BUG: Unknown operator");
                   break;
             } 
             if (TraceCode)  emitComment("<- op") ;
             break; 


          case CALL_AST:
             if (TraceCode) emitComment("-> call") ;
             p1 = tree->child[0];/*arguments*/

             while(p1 != NULL){
                pushParam(p1);
                p1 = p1->sibling;
             }

            /* first - push parameters */
             isRecursive = 0;
             while( (p1 = popParam()) != NULL){
                cGen(p1);
                emitRM("LDA",sp,-1,sp,"push prepare");
                emitRM("ST",ax,0,sp,"push parameters");
             }
             isRecursive = 1;

             /*second - call function*/
             fun = lookup_fun(tree->attr.name);
             emitCall(fun);
             

             if (TraceCode)  emitComment("<- call") ;
             break; 

          default:
            break;
        }

      if(isRecursive)
        tree = tree->sibling;
      else
        break;
    }
}



/* the primary function of the code generator */
void codeGen()
{  


   emitPrelude();


   /* Jump to main() */
   if (TraceCode) emitComment("Jump to main()");
   int loc = emitSkip(6); /*A call consumes 5 instructions, and we need halt after main()*/


   emitInput();
   emitOutput();

   
   cGen(ASTRoot);

   /* Fill up jump-to-main code */
   emitBackup(loc);
   FunSymbol* fun = lookup_fun("main");
   emitCall(fun);
   emitRO("HALT",0,0,0,"END OF PROGRAM");

}