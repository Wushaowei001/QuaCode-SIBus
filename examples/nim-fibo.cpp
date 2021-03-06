/* -*- mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*- */
/*
 *  Main authors:
 *     Vincent Barichard <Vincent.Barichard@univ-angers.fr>
 *
 *  Copyright:
 *     Vincent Barichard, 2013
 *
 *  Last modified:
 *     $Date$ by $Author$
 *     $Revision$
 *
 *  This file is part of Quacode:
 *     http://quacode.barichard.com
 *
 *  Permission is hereby granted, free of charge, to any person obtaining
 *  a copy of this software and associated documentation files (the
 *  "Software"), to deal in the Software without restriction, including
 *  without limitation the rights to use, copy, modify, merge, publish,
 *  distribute, sublicense, and/or sell copies of the Software, and to
 *  permit persons to whom the Software is furnished to do so, subject to
 *  the following conditions:
 *
 *  The above copyright notice and this permission notice shall be
 *  included in all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 *  LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 *  OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 *  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <iostream>
#include <vector>

#include <quacode/qspaceinfo.hh>
#include <gecode/minimodel.hh>
#include <gecode/driver.hh>

#include <sibus/receivers/receiver-out.hh>
#include <sibus/receivers/receiver-nodecount.hh>

using namespace Gecode;

#ifdef GECODE_HAS_GIST
namespace Gecode { namespace Driver {
  /// Specialization for QDFS
  template<typename S>
  class GistEngine<QDFS<S> > {
  public:
    static void explore(S* root, const Gist::Options& opt) {
      (void) Gist::explore(root, false, opt);
    }
  };
}}
#endif

/**
 * \brief Options taking one additional parameter
 */
class NimFiboOptions : public Options {
public:
  int n; /// Parameter to be given on the command line
  /// Use cut or not
  Gecode::Driver::BoolOption _cut;
  /// Initialize options for example with name \a s
  NimFiboOptions(const char* s, int n0)
    : Options(s), n(n0),
      _cut("-cut","Use cut in problem model",true) {
        add(_cut);
      }
  /// Parse options from arguments \a argv (number is \a argc)
  void parse(int& argc, char* argv[]) {
    Options::parse(argc,argv);
    if (argc < 2)
      return;
    n = atoi(argv[1]);
  }
  /// Print help message
  virtual void help(void) {
    Options::help();
    std::cerr << "\t(unsigned int) default: " << n << std::endl
              << "\t\tnumber of initial matchs" << std::endl;
  }
  /// Return true if cut must be used
  bool cut(void) const {
    return _cut.value();
  }
};

/// Succeed the space
static void gf_success(Space& home) {
  Space::Branchers b(home);
  while (b()) {
    BrancherHandle bh(b.brancher());
    ++b;
    bh.kill(home);
  }
}

/// Dummy function
static void gf_dummy(Space& ) { }

/// Adding cut
static void cut(Space& home, const BoolExpr& expr) {
  BoolVar o(home,0,1);
  rel(home, o == expr);
  when(home, o, &gf_success, &gf_dummy);
}

class QCSPNimFibo : public Script, public QSpaceInfo {
  IntVarArray X;

public:

  QCSPNimFibo(const NimFiboOptions& opt) : Script(), QSpaceInfo()
  {
    // DEBUT DESCRIPTION PB
    std::cout << "Loading problem" << std::endl;
    using namespace Int;
    // Number of matches
    int NMatchs = opt.n;
    int nIterations = (NMatchs%2)?NMatchs:NMatchs+1;

    IntVarArgs x;
    SIBus::instance().sendVar(TVarBinder(EXISTS,"x1",TYPE_INT,TVal(1,NMatchs-1)));
    X = IntVarArray(*this,nIterations,1,NMatchs-1);
    x << X[0];

    BoolVar o_im1(*this,1,1);
    BoolExpr cutExpr1(BoolVar(*this,1,1));
    BoolExpr cutExpr2(BoolVar(*this,1,1));
    //branch(*this, X[0], INT_VAR_NONE(), INT_VALUES_MIN());
    branch(*this, X[0], INT_VAR_NONE(), INT_VAL_MIN());

    for (int i=1; i < nIterations; i++) {
      BoolVar oi(*this,0,1), o1(*this,0,1), o2(*this,0,1);

      x << X[i];

      rel(*this, X[i], IRT_LQ, expr(*this, 2*X[i-1]), o1);
      linear(*this, x, IRT_LQ, NMatchs, o2);

      if (i%2) {
        // Universal Player
        setForAll(*this,X[i]);
        std::stringstream ss_y; ss_y << "y" << i/2 + 1;
        SIBus::instance().sendVar(TVarBinder(FORALL,ss_y.str(),TYPE_INT,TVal(1,NMatchs-1)));
        rel(*this, o_im1 == ((o1 && o2) >> oi));
        // Adding Cut
        if (opt.cut())
          cut(*this, cutExpr1 && cutExpr2 && !(o1 && o2));  // Universal player can't play
        //branch(*this, X[i], INT_VAR_NONE(), INT_VALUES_MIN());
        branch(*this, X[i], INT_VAR_NONE(), INT_VAL_MIN());
      } else {
        // Existantial Player
        std::stringstream ss_x; ss_x << "x" << i/2 + 1;
        SIBus::instance().sendVar(TVarBinder(EXISTS,ss_x.str(),TYPE_INT,TVal(1,NMatchs-1)));
        rel(*this, o_im1 == (o1 && o2 && oi));
        //branch(*this, X[i], INT_VAR_NONE(), INT_VALUES_MIN());
        branch(*this, X[i], INT_VAR_NONE(), INT_VAL_MIN());
      }
      cutExpr1 = cutExpr1 && o1;
      cutExpr2 = o2;
      o_im1 = oi;
    }

    // FIN DESCRIPTION PB
    SIBus::instance().sendCloseModeling();
  }

  QCSPNimFibo(bool share, QCSPNimFibo& p)
    : Script(share,p), QSpaceInfo(*this,share,p)
  {
    X.update(*this,share,p.X);
  }

  virtual Space* copy(bool share) { return new QCSPNimFibo(share,*this); }

  void eventNewInstance(void) const {
    TInstance instance;
    for (int i=0; i<X.size(); i++)
    {
      if (!X[i].varimp()->assigned())
        instance.push_back(TVal());
      else
        instance.push_back(TVal(X[i].varimp()->val()));
    }
    SIBus::instance().sendInstance(instance);
  }

  void print(std::ostream& os) const {
    strategyPrint(os);
  }
};

int main(int argc, char* argv[])
{
  ProcessSTDOUT pStdout;
  ProcessNodeCount pNodeCount;

  SIBus::create();
//  SIBus::instance().addReceiver(pStdout);
  SIBus::instance().addReceiver(pNodeCount);

  NimFiboOptions opt("QCSP Nim-Fibo",3);
  opt.parse(argc,argv);
  Script::run<QCSPNimFibo,QDFS,NimFiboOptions>(opt);

  SIBus::kill();
  return 0;
}

