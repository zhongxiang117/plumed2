/* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
   Copyright (c) 2016-2017 The VES code team
   (see the PEOPLE-VES file at the root of this folder for a list of names)

   See http://www.ves-code.org for more information.

   This file is part of VES code module.

   The VES code module is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   The VES code module is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with the VES code module.  If not, see <http://www.gnu.org/licenses/>.
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ */


#include "BasisFunctions.h"
#include "DbWaveletGrid.h"
#include "tools/Grid.h"
#include "VesTools.h"
#include "core/ActionRegister.h"


namespace PLMD {
namespace ves {


//+PLUMEDOC VES_BASISF BF_DB_WAVELETS
/*
Daubechies Wavelets as basis functions

Note: at the moment only the scaling function and not the wavelet function is used.
It should nevertheless form an orthogonal basis set and will be needed for multiscale.
The Wavelet function can be easily implemented by an additional matrix multiplication and a translation of the position axis.

order N: number of vanishing moments

Support is then [0, 2*N-1), each basis function is a translate by an integer value k.

If the support is scaled to match the desired range of the CV exactly there would be 4*N -3 basis functions whose support is at least partially in this region: k = {(-2*N)+2, … , 0, … 2*N-1}
Especially for the scaling function the translates with support in negative regions do not have significant contributions in the desired range if k <= -order, so these are omitted by default. (could cut maybe one more)

The default range of k is therefore only k = {-N+1, -N+2, …, 0, …, 2*N-1}.
Including a constant basis function this sums to N*3-1 used basis functions by default.
This could be lowered by scaling the wavelets less so that their support is larger than the desired CV range.

Method of construction: Strang, Nguyen - Vector cascade algorithm (Daubechies-Lagarias method)

\par Examples

\par Test

*/
//+ENDPLUMEDOC


class BF_DbWavelets : public BasisFunctions {
  // Grid that holds the Wavelet values and its derivative
  std::unique_ptr<Grid> waveletGrid_;
  bool use_scaling_function_;
  void setupLabels() override;

public:
  static void registerKeywords( Keywords&);
  explicit BF_DbWavelets(const ActionOptions&);
  void getAllValues(const double, double&, bool&, std::vector<double>&, std::vector<double>&) const override;
};


PLUMED_REGISTER_ACTION(BF_DbWavelets,"BF_DB_WAVELETS")


void BF_DbWavelets::registerKeywords(Keywords& keys) {
  BasisFunctions::registerKeywords(keys);
  keys.add("optional","GRID_SIZE","The number of grid bins of the Wavelet function. Because of the used construction algorithm this value will be used as guiding value only, while the true number will be \"(ORDER*2 - 1) * 2**n\" with the smallest n such that the grid is at least as large as the specified number. Defaults to 1000"); // Change the commentary a bit?
  keys.addFlag("SCALING_FUNCTION", false, "If this flag is set the scaling function (mother wavelet) will be used instead of the \"true\" wavelet function (father wavelet).");
  keys.addFlag("DUMP_WAVELET_GRID", false, "If this flag is set the grid with the wavelet values will be written to a file called \"wavelet_grid.data\".");
  // why is this removed?
  keys.remove("NUMERICAL_INTEGRALS");
}


BF_DbWavelets::BF_DbWavelets(const ActionOptions&ao):
  PLUMED_VES_BASISFUNCTIONS_INIT(ao),
  use_scaling_function_(false)
{
  setNumberOfBasisFunctions((getOrder()*3)-1);

  // parse grid properties and set it up
  parseFlag("SCALING_FUNCTION", use_scaling_function_);
  unsigned gridsize = 1000;
  parse("GRID_SIZE", gridsize);
  waveletGrid_ = DbWaveletGrid::setupGrid(getOrder(), gridsize, !use_scaling_function_);
  unsigned true_gridsize = waveletGrid_->getNbin()[0];
  if(true_gridsize != 1000) {addKeywordToList("GRID_SIZE",true_gridsize);}
  bool dump_wavelet_grid=false;
  parseFlag("DUMP_WAVELET_GRID", dump_wavelet_grid);
  if (dump_wavelet_grid) {
    OFile wavelet_gridfile;
    wavelet_gridfile.open(getLabel()+".wavelet_grid.data");
    waveletGrid_->writeToFile(wavelet_gridfile);
  }

  // set some properties
  setIntrinsicInterval("0",std::to_string(getNumberOfBasisFunctions()-1));
  setNonPeriodic();
  setIntervalBounded();
  setType("daubechies_wavelets");
  setDescription("Daubechies Wavelets (maximum phase type)");
  setLabelPrefix("k");
  setupBF();
  checkRead();
}


void BF_DbWavelets::getAllValues(const double arg, double& argT, bool& inside_range, std::vector<double>& values, std::vector<double>& derivs) const {
  // plumed_assert(values.size()==numberOfBasisFunctions());
  // plumed_assert(derivs.size()==numberOfBasisFunctions());
  //
  argT=checkIfArgumentInsideInterval(arg,inside_range);
  //
  values[0]=1.0;
  derivs[0]=0.0;
  for(unsigned int i = 1; i < getNumberOfBasisFunctions(); ++i) {
    // calculate translation of wavelet
    int k = i - getOrder();
    // scale and shift argument to match current wavelet
    double x = (arg-intervalMin())*intervalDerivf() - k;

    if (x < 0 || x > intrinsicIntervalMax()) { // Wavelets are 0 outside the defined range
      values[i] = 0.0; derivs[i] = 0.0;
    }
    else {
      // declaring vectors and calling first a function to get the index is a bit cumbersome and might be slow
      std::vector<double> temp_deriv;
      std::vector<double> x_vec {x};
      values[i] = waveletGrid_->getValueAndDerivatives(x_vec, temp_deriv);
      derivs[i] = temp_deriv[0] * intervalDerivf(); // scale derivative
    }
  }
  if(!inside_range) {for(auto& deriv : derivs) {deriv=0.0;}}
}


// label according to positions?
void BF_DbWavelets::setupLabels() {
  setLabel(0,"const");
  double min = intervalMin();
  double spacing = intervalDerivf();
  for(unsigned int i=1; i < getNumberOfBasisFunctions(); i++) {
    double pos = min + (i-1) / spacing;
    std::string is; Tools::convert(pos, is);
    setLabel(i,"i = "+is);
  }
}


}
}
