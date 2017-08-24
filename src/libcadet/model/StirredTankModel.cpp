// =============================================================================
//  CADET - The Chromatography Analysis and Design Toolkit
//
//  Copyright © 2008-2017: The CADET Authors
//            Please see the AUTHORS and CONTRIBUTORS file.
//
//  All rights reserved. This program and the accompanying materials
//  are made available under the terms of the GNU Public License v3.0 (or, at
//  your option, any later version) which accompanies this distribution, and
//  is available at http://www.gnu.org/licenses/gpl.html
// =============================================================================

#include "model/StirredTankModel.hpp"
#include "ParamReaderHelper.hpp"
#include "cadet/Exceptions.hpp"
#include "cadet/SolutionRecorder.hpp"
#include "model/BindingModel.hpp"

#include "ConfigurationHelper.hpp"
#include "ParamIdUtil.hpp"
#include "linalg/Norms.hpp"

#include <algorithm>
#include <functional>

#include "LoggingUtils.hpp"
#include "Logging.hpp"
#include "AdUtils.hpp"
#include "SensParamUtil.hpp"

namespace cadet
{

namespace model
{

CSTRModel::CSTRModel(UnitOpIdx unitOpIdx) : _unitOpIdx(unitOpIdx), _nComp(0), _nBound(nullptr), _boundOffset(nullptr), _strideBound(0), 
	_binding(nullptr), _analyticJac(true), _jac(), _jacFact(), _factorizeJac(false), _consistentInitBuffer(nullptr)
{
}

CSTRModel::~CSTRModel() CADET_NOEXCEPT
{
	delete[] _boundOffset;
	delete[] _nBound;
	delete[] _consistentInitBuffer;
}

unsigned int CSTRModel::numDofs() const CADET_NOEXCEPT
{
	return 2 *_nComp + _strideBound + 1;
}

unsigned int CSTRModel::numPureDofs() const CADET_NOEXCEPT
{
	return _nComp + _strideBound + 1;
}

bool CSTRModel::usesAD() const CADET_NOEXCEPT
{
#ifdef CADET_CHECK_ANALYTIC_JACOBIAN
	// We always need AD if we want to check the analytical Jacobian
	return true;
#else
	// We only need AD if we are not computing the Jacobian analytically
	return !_analyticJac;
#endif
}

void CSTRModel::setFlowRates(const active& in, const active& out) CADET_NOEXCEPT 
{ 
	_flowRateIn = in;
	_flowRateOut = out;
}

bool CSTRModel::configure(IParameterProvider& paramProvider, IConfigHelper& helper)
{
	_nComp = paramProvider.getInt("NCOMP");

	_nBound = new unsigned int[_nComp];
	if (paramProvider.exists("NBOUND"))
	{
		const std::vector<int> nBound = paramProvider.getIntArray("NBOUND");
		std::copy(nBound.begin(), nBound.end(), _nBound);
	}
	else
		std::fill_n(_nBound, _nComp, 0u);

	// Precompute offsets and total number of bound states (DOFs in solid phase)
	_boundOffset = new unsigned int[_nComp];
	_boundOffset[0] = 0;
	for (unsigned int i = 1; i < _nComp; ++i)
	{
		_boundOffset[i] = _boundOffset[i-1] + _nBound[i-1];
	}
	_strideBound = _boundOffset[_nComp-1] + _nBound[_nComp - 1];

	// Allocate Jacobian
	const unsigned int nVar = _nComp + _strideBound + 1;
	_jac.resize(nVar, nVar);
	_jacFact.resize(nVar, nVar);

	// Determine whether analytic Jacobian should be used
#ifndef CADET_CHECK_ANALYTIC_JACOBIAN
	bool analyticJac = true;
	if (paramProvider.exists("USE_ANALYTIC_JACOBIAN"))
		analyticJac = paramProvider.getBool("USE_ANALYTIC_JACOBIAN");
#else
	// Default to AD Jacobian when analytic Jacobian is to be checked
	const bool analyticJac = false;
#endif
	useAnalyticJacobian(analyticJac);

	reconfigure(paramProvider);

	// ==== Construct and configure binding model
	delete _binding;

	if (paramProvider.exists("ADSORPTION_MODEL"))
	{
		_binding = helper.createBindingModel(paramProvider.getString("ADSORPTION_MODEL"));
		if (!_binding)
			throw InvalidParameterException("Unknown binding model " + paramProvider.getString("ADSORPTION_MODEL"));

		_binding->configureModelDiscretization(_nComp, _nBound, _boundOffset);

		paramProvider.pushScope("adsorption");
		const bool bindingConfSuccess = _binding->configure(paramProvider, _unitOpIdx);
		paramProvider.popScope();

		// Allocate memory for nonlinear equation solving
		unsigned int size = 0;
		if (_binding->hasAlgebraicEquations())
		{
			// Required memory (number of doubles) for nonlinear solvers
			size = _binding->consistentInitializationWorkspaceSize();
		}
		if (size > 0)
			_consistentInitBuffer = new double[size];

		return bindingConfSuccess;
	}
	else
		_binding = helper.createBindingModel("NONE");

	return true;
}

bool CSTRModel::reconfigure(IParameterProvider& paramProvider)
{
	_curFlowRateFilter = 0.0;
	_flowRateFilter.clear();
	const bool hasFlowrateFilter = paramProvider.exists("FLOWRATE_FILTER");
	if (hasFlowrateFilter)
		readScalarParameterOrArray(_flowRateFilter, paramProvider, "FLOWRATE_FILTER", 1);

	_porosity = 1.0;
	if (paramProvider.exists("POROSITY"))
		_porosity = paramProvider.getDouble("POROSITY");

	_parameters.clear();
	if (hasFlowrateFilter)
		registerScalarSectionDependentParam(hashString("FLOWRATE_FILTER"), _parameters, _flowRateFilter, _unitOpIdx);
	_parameters[makeParamId(hashString("POROSITY"), _unitOpIdx, CompIndep, BoundPhaseIndep, ReactionIndep, SectionIndep)] = &_porosity;

	// Reconfigure binding model
	if (_binding && paramProvider.exists("adsorption"))
	{
		paramProvider.pushScope("adsorption");
		const bool bindingConfSuccess = _binding->reconfigure(paramProvider, _unitOpIdx);
		paramProvider.popScope();

		return bindingConfSuccess;
	}

	return true;
}

void CSTRModel::setSectionTimes(double const* secTimes, bool const* secContinuity, unsigned int nSections)
{
}

std::unordered_map<ParameterId, double> CSTRModel::getAllParameterValues() const
{
	std::unordered_map<ParameterId, double> data;
	std::transform(_parameters.begin(), _parameters.end(), std::inserter(data, data.end()),
	               [](const std::pair<const ParameterId, active*>& p) { return std::make_pair(p.first, static_cast<double>(*p.second)); });
	if (!_binding)
		return data;

	const std::unordered_map<ParameterId, double> localData = _binding->getAllParameterValues();
	for (const std::pair<ParameterId, double>& val : localData)
		data[val.first] = val.second;

	return data;
}

bool CSTRModel::hasParameter(const ParameterId& pId) const
{
	const bool hasParam = _parameters.find(pId) != _parameters.end();
	if (_binding)
		return hasParam || _binding->hasParameter(pId);
	return hasParam;
}

bool CSTRModel::setParameter(const ParameterId& pId, int value)
{
	if ((pId.unitOperation != _unitOpIdx) && (pId.unitOperation != UnitOpIndep))
		return false;

	if (_binding)
		return _binding->setParameter(pId, value);
	return false;
}

bool CSTRModel::setParameter(const ParameterId& pId, double value)
{
	if ((pId.unitOperation != _unitOpIdx) && (pId.unitOperation != UnitOpIndep))
		return false;

	auto paramHandle = _parameters.find(pId);
	if (paramHandle != _parameters.end())
	{
		paramHandle->second->setValue(value);
		return true;
	}
	else if (_binding)
		return _binding->setParameter(pId, value);

	return false;
}

bool CSTRModel::setParameter(const ParameterId& pId, bool value)
{
	if ((pId.unitOperation != _unitOpIdx) && (pId.unitOperation != UnitOpIndep))
		return false;

	if (_binding)
		return _binding->setParameter(pId, value);
	return false;
}

void CSTRModel::setSensitiveParameterValue(const ParameterId& pId, double value)
{
	if ((pId.unitOperation != _unitOpIdx) && (pId.unitOperation != UnitOpIndep))
		return;

	// Check our own parameters
	auto paramHandle = _parameters.find(pId);
	if ((paramHandle != _parameters.end()) && contains(_sensParams, paramHandle->second))
	{
		paramHandle->second->setValue(value);
		return;
	}

	// Check binding model parameters
	if (_binding)
	{
		active* const val = _binding->getParameter(pId);
		if (val && contains(_sensParams, val))
		{
			val->setValue(value);
			return;
		}
	}
}

bool CSTRModel::setSensitiveParameter(const ParameterId& pId, unsigned int adDirection, double adValue)
{
	if ((pId.unitOperation != _unitOpIdx) && (pId.unitOperation != UnitOpIndep))
		return false;

	// Check own parameters
	auto paramHandle = _parameters.find(pId);
	if (paramHandle != _parameters.end())
	{
		LOG(Debug) << "Found parameter " << pId << " in GRM: Dir " << adDirection << " is set to " << adValue;

		// Register parameter and set AD seed / direction
		_sensParams.insert(paramHandle->second);
		paramHandle->second->setADValue(adDirection, adValue);
		return true;
	}

	// Check binding model parameters
	if (_binding)
	{
		active* const paramBinding = _binding->getParameter(pId);
		if (paramBinding)
		{
			LOG(Debug) << "Found parameter " << pId << " in AdsorptionModel: Dir " << adDirection << " is set to " << adValue;

			// Register parameter and set AD seed / direction
			_sensParams.insert(paramBinding);
			paramBinding->setADValue(adDirection, adValue);
			return true;
		}
	}

	return false;
}

void CSTRModel::clearSensParams()
{
	// Remove AD directions from parameters
	for (auto sp : _sensParams)
		sp->setADValue(0.0);

	_sensParams.clear();
}

void CSTRModel::useAnalyticJacobian(const bool analyticJac)
{
#ifndef CADET_CHECK_ANALYTIC_JACOBIAN
	_analyticJac = analyticJac;
#else
	// Use AD Jacobian if analytic Jacobian is to be checked
	_analyticJac = false;
#endif
}

void CSTRModel::notifyDiscontinuousSectionTransition(double t, unsigned int secIdx, active* const adRes, active* const adY, unsigned int adDirOffset)
{
	if (_flowRateFilter.size() > 1)
	{
		_curFlowRateFilter = _flowRateFilter[secIdx];
	}
	else if (_flowRateFilter.size() == 1)
	{
		_curFlowRateFilter = _flowRateFilter[0];
	}
}

void CSTRModel::reportSolution(ISolutionRecorder& recorder, double const* const solution) const
{
	Exporter expr(_nComp, _nBound, _strideBound, _boundOffset, solution);
	recorder.beginUnitOperation(_unitOpIdx, *this, expr);
	recorder.endUnitOperation();
}

void CSTRModel::reportSolutionStructure(ISolutionRecorder& recorder) const
{
	Exporter expr(_nComp, _nBound, _strideBound, _boundOffset, nullptr);
	recorder.unitOperationStructure(_unitOpIdx, *this, expr);
}

unsigned int CSTRModel::requiredADdirs() const CADET_NOEXCEPT
{
	return _nComp + _strideBound + 1;
}

void CSTRModel::prepareADvectors(active* const adRes, active* const adY, unsigned int adDirOffset) const
{
	// Early out if AD is disabled
	if (!adY)
		return;

	ad::prepareAdVectorSeedsForDenseMatrix(adY + _nComp, adDirOffset, _jac.rows(), _jac.columns());
}

void CSTRModel::applyInitialCondition(double* const vecStateY, double* const vecStateYdot)
{
	std::fill(vecStateY, vecStateY + numDofs(), 0.0);
	std::fill(vecStateYdot, vecStateYdot + numDofs(), 0.0);
}

void CSTRModel::applyInitialCondition(IParameterProvider& paramProvider, double* const vecStateY, double* const vecStateYdot)
{
	// Check if INIT_STATE is present
	if (paramProvider.exists("INIT_STATE"))
	{
		const std::vector<double> initState = paramProvider.getDoubleArray("INIT_STATE");
		std::copy(initState.data(), initState.data() + numDofs(), vecStateY);

		// Check if INIT_STATE contains the full state and its time derivative
		if (initState.size() >= 2 * numDofs())
		{
			double const* const srcYdot = initState.data() + numDofs();
			std::copy(srcYdot, srcYdot + numDofs(), vecStateYdot);
		}
		return;
	}

	const std::vector<double> initC = paramProvider.getDoubleArray("INIT_C");

	if (initC.size() < _nComp)
		throw InvalidParameterException("INIT_C does not contain enough values for all components");
	
	std::copy_n(initC.begin(), _nComp, vecStateY + _nComp);

	if (paramProvider.exists("INIT_Q"))
	{
		const std::vector<double> initQ = paramProvider.getDoubleArray("INIT_Q");
		std::copy_n(initQ.begin(), _strideBound, vecStateY + 2 * _nComp);
	}
	else
		std::fill_n(vecStateY + 2 * _nComp, _strideBound, 0.0);

	if (paramProvider.exists("INIT_VOLUME"))
		vecStateY[2 * _nComp + _strideBound] = paramProvider.getDouble("INIT_VOLUME");
	else
		vecStateY[2 * _nComp + _strideBound] = 0.0;
}


void CSTRModel::consistentInitialState(double t, unsigned int secIdx, double timeFactor, double* const vecStateY, active* const adRes, active* const adY, unsigned int adDirOffset, double errorTol)
{
	double * const c = vecStateY + _nComp;
	const double v = c[_nComp];

	// Check if volume is 0
	if (v == 0.0)
	{
		const double flowIn = static_cast<double>(_flowRateIn);
		const double flowOut = static_cast<double>(_flowRateOut);

		// Volume: \dot{V} = F_{in} - F_{out} - F_{filter}
		const double vDot = flowIn - flowOut - static_cast<double>(_curFlowRateFilter);

		// We have the equation
		//    V * \dot{c} + \dot{V} * c = c_in * F_in - c * F_out
		// which is now algebraic wrt. c due to V = 0:
		//    \dot{V} * c = c_in * F_in - c * F_out
		// Separating knowns from unknowns gives
		//    (\dot{V} + F_out) * c = c_in * F_in
		// Hence, we obtain
		//    c = c_in * F_in / (\dot{V} + F_out)

		// Note that if the denominator were 0, we had
		//    0 = \dot{V} + F_out = F_{in} - F_{filter}
		// which leads to
		//    F_in = F_filter
		// Since F_out >= 0 and \dot{V} = -F_out, we get
		//    \dot{V} <= 0
		// Assuming a valid configuration, we obtain \dot{V} = 0
		// as the tank would get a negative volume otherwise.
		// Concluding, we arrive at \dot{V} = F_out = 0.
		// In this situation, F_in = F_filter = 0 has to hold
		// as otherwise the liquid (solvent) is immediately and
		// fully taken out, leaving only the pure dry components.
		// We, hence, assume that this doesn't happen and simply
		// do nothing leaving the initial conditions in place.

		const double denom = vDot + flowOut;
		if (denom != 0.0)
		{
			const double factor = flowIn / denom;
			for (unsigned int i = 0; i < _nComp; i++)
			{
				c[i] = vecStateY[i] * factor;
			}
		}
	}
}

void CSTRModel::consistentInitialTimeDerivative(double t, unsigned int secIdx, double timeFactor, double const* vecStateY, double* const vecStateYdot) 
{
	double const* const c = vecStateY + _nComp;
	double* const cDot = vecStateYdot + _nComp;
	const double v = c[_nComp];

	const double flowIn = static_cast<double>(_flowRateIn);
	const double flowOut = static_cast<double>(_flowRateOut);

	// Volume: \dot{V} = F_{in} - F_{out} - F_{filter}
	const double vDot = flowIn - flowOut - static_cast<double>(_curFlowRateFilter);
	cDot[_nComp] = vDot;

	// Check if volume is 0
	if (v == 0.0)
	{
		// We have the equation
		//    V * \dot{c} + \dot{V} * c = c_in * F_in - c * F_out
		// which is now algebraic wrt. c due to V = 0:
		//    \dot{V} * c = c_in * F_in - c * F_out
		// So we take the derivative wrt. to time t on both sides
		//    2 * \dot{V} * \dot{c} + V * \ddot{c} + \ddot{V} * c = \dot{c}_in * F_in - \dot{c} * F_out
		// and use the fact that \ddot{V} = 0 and V = 0 to arrive at
		//    2 * \dot{V} * \dot{c} = \dot{c}_in * F_in - \dot{c} * F_out
		// Separating knowns from unknowns gives
		//    (2 * \dot{V} + F_out) * \dot{c} = \dot{c}_in * F_in
		// which finally yields
		//    \dot{c} = \dot{c}_in * F_in / (2 * \dot{V} + F_out)

		// Note that if the denominator were 0, we had
		//    0 = 2 * \dot{V} + F_out = 2 * F_in - 2 * F_filter - F_out
		// which leads to
		//    F_out = 2 * F_in - 2 * F_filter                       (*)
		// Plugging this back into the \dot{V} equation gives
		//    \dot{V} = F_in - F_out - F_filter = -F_in + F_filter
		// Since V = 0, a valid choice of parameters has to ensure
		// \dot{V} >= 0. In this case, we obtain
		//    F_in <= F_filter
		// On the other hand, we infer from (*) that
		//    0 <= F_out <= 0   =>   F_out = 0   =>   F_in = F_filter
		// This, in turn, concludes \dot{V} = 0. In this situation, 
		//    F_in = F_filter = 0
		// has to hold as otherwise the liquid (solvent) is immediately
		// and fully taken out, leaving only the pure dry components.
		// We, hence, assume that this doesn't happen. Summarizing, we
		// have
		//    \dot{V} = 0, F_in = F_filter = F_out = 0
		// and nothing can happen or change. Therefore, \dot{c} is set
		// to 0.0.

		const double denom = 2.0 * vDot + flowOut;
		if (denom == 0.0)
		{
			// Assume F_in = F_filter = 0
			std::fill(cDot, cDot + _nComp, 0.0);
		}
		else
		{
			const double factor = flowIn / denom;
			for (unsigned int i = 0; i < _nComp; i++)
			{
				// TODO: This is wrong as vecStateYdot does not contain \dot{c}_in (on entry)
				vecStateYdot[i] = 0.0;
				cDot[i] = vecStateYdot[i] * factor;
			}
		}
	}
	else
	{
		// Concentrations: V * \dot{c} = c_in * F_in - c * F_out - \dot{V} * c
		//                             = -vecStateYdot - \dot{V} * c
		// => \dot{c} = (-vecStateYdot - \dot{V} * c) / V
		for (unsigned int i = 0; i < _nComp; i++)
		{
			cDot[i] = (-cDot[i] - vDot * c[i]) / v;
		}
	}
}

void CSTRModel::leanConsistentInitialState(double t, unsigned int secIdx, double timeFactor, double* const vecStateY, active* const adRes, active* const adY, unsigned int adDirOffset, double errorTol)
{
	consistentInitialState(t, secIdx, timeFactor, vecStateY, adRes, adY, adDirOffset, errorTol);
}

void CSTRModel::leanConsistentInitialSensitivity(const active& t, unsigned int secIdx, const active& timeFactor, double const* vecStateY, double const* vecStateYdot,
	std::vector<double*>& vecSensY, std::vector<double*>& vecSensYdot, active const* const adRes)
{
	consistentInitialSensitivity(t, secIdx, timeFactor, vecStateY, vecStateYdot, vecSensY, vecSensYdot, adRes);
}

int CSTRModel::residual(double t, unsigned int secIdx, double timeFactor, double const* const y, double const* const yDot, double* const res)
{
	return residualImpl<double, double, double, false>(t, secIdx, timeFactor, y, yDot, res);
}

template <typename StateType, typename ResidualType, typename ParamType, bool wantJac>
int CSTRModel::residualImpl(const ParamType& t, unsigned int secIdx, const ParamType& timeFactor, StateType const* const y, double const* const yDot, ResidualType* const res)
{
	StateType const* const cIn = y;
	StateType const* const c = y + _nComp;
	const StateType& v = y[2 * _nComp + _strideBound];

	double const* const cDot = yDot ? yDot + _nComp : nullptr;
	const double vDot = yDot ? yDot[2 * _nComp + _strideBound] : 0.0;

	const ParamType flowIn = static_cast<ParamType>(_flowRateIn);
	const ParamType flowOut = static_cast<ParamType>(_flowRateOut);

	// Inlet DOF
	for (unsigned int i = 0; i < _nComp; ++i)
	{
		res[i] = cIn[i];
	}

	// Concentrations: \dot{V} * c + V * \dot{c} = c_in * F_in - c * F_out
	const ParamType invBeta = 1.0 / static_cast<ParamType>(_porosity) - 1.0;
	ResidualType* const resC = res + _nComp;
	for (unsigned int i = 0; i < _nComp; ++i)
	{
		resC[i] = 0.0;
		const unsigned int nBound = _nBound[i];

		// Add time derivatives
		if (yDot)
		{
			// Ultimately, we need (dc_{i} / dt + 1 / beta * [ sum_j  dq_{i,j} / dt ]) * V
			// Compute the sum in the brackets first, then divide by beta and add dc / dt

			// Sum dq_{i,1} / dt + dq_{i,2} / dt + ... + dq_{i,N_i} / dt
			double const* const qDot = cDot + _nComp + _boundOffset[i];
			for (unsigned int j = 0; i < nBound; ++i)
				resC[i] += qDot[j];

			// Divide by beta and add dc_i / dt
			resC[i] = timeFactor * ((cDot[i] + invBeta * resC[i]) * v + vDot * c[i]);
		}

		resC[i] += -flowIn * cIn[i] + flowOut * c[i];
	}

	// Bound states
	_binding->residual(t, 0.0, 0.0, secIdx, timeFactor, c, cDot, res + 2 * _nComp);

	// Volume: \dot{V} = F_{in} - F_{out} - F_{filter}
	res[2 * _nComp + _strideBound] = vDot - flowIn + flowOut + static_cast<ParamType>(_curFlowRateFilter);

	if (wantJac)
	{
		_jac.setAll(0.0);

		// Assemble Jacobian: dRes / dy

		// Concentrations: \dot{V} * (c_i + invBeta * sum_j q_{i,j}) + V * (\dot{c}_i + invBeta * sum_j \dot{q}_{i,j}) - c_{in,i} * F_in + c_i * F_out == 0
		for (unsigned int i = 0; i < _nComp; i++)
		{
			_jac.native(i, i) = static_cast<double>(vDot) + static_cast<double>(flowOut);

			double qSum = 0.0;
			double const* const qiDot = cDot + _nComp + _boundOffset[i];
			const unsigned int localOffset = _nComp + _boundOffset[i];
			const double vDotInvBeta = static_cast<double>(vDot) * static_cast<double>(invBeta);
			for (unsigned int j = 0; j < _nBound[i]; ++j)
			{
				_jac.native(i, localOffset + j) = vDotInvBeta;
				// + _nComp: Moves over liquid phase components
				// + _boundOffset[i]: Moves over bound states of previous components
				// + j: Moves to current bound state j of component i

				qSum += qiDot[j];
			}

			_jac.native(i, _nComp + _strideBound) = cDot[i] + static_cast<double>(invBeta) * qSum;
		}

		// Bound states
		_binding->analyticJacobian(static_cast<double>(t), 0.0, 0.0, secIdx, reinterpret_cast<double const*>(y) + 2 * _nComp, _jac.row(_nComp));

		// Volume: \dot{V} - F_{in} + F_{out} + F_{filter} == 0
	}

	return 0;
}

int CSTRModel::residual(const active& t, unsigned int secIdx, const active& timeFactor, double const* const y, double const* const yDot, double* const res,
	active* const adRes, active* const adY, unsigned int adDirOffset, bool updateJacobian, bool paramSensitivity)
{
	if (updateJacobian)
	{
		_factorizeJac = true;

#ifndef CADET_CHECK_ANALYTIC_JACOBIAN
		if (_analyticJac)
		{
			if (paramSensitivity)
			{
				const int retCode = residualImpl<double, active, active, true>(t, secIdx, timeFactor, y, yDot, adRes);

				// Copy AD residuals to original residuals vector
				if (res)
					ad::copyFromAd(adRes, res, numDofs());

				return retCode;
			}
			else
				return residualImpl<double, double, double, true>(static_cast<double>(t), secIdx, static_cast<double>(timeFactor), y, yDot, res);
		}
		else
		{
			// Compute Jacobian via AD

			// Copy over state vector to AD state vector (without changing directional values to keep seed vectors)
			// and initalize residuals with zero (also resetting directional values)
			ad::copyToAd(y, adY, numDofs());
			// @todo Check if this is necessary
			ad::resetAd(adRes, numDofs());

			// Evaluate with AD enabled
			int retCode = 0;
			if (paramSensitivity)
				retCode = residualImpl<active, active, active, false>(t, secIdx, timeFactor, adY, yDot, adRes);
			else
				retCode = residualImpl<active, active, double, false>(static_cast<double>(t), secIdx, static_cast<double>(timeFactor), adY, yDot, adRes);

			// Copy AD residuals to original residuals vector
			if (res)
				ad::copyFromAd(adRes, res, numDofs());

			// Extract Jacobian
			extractJacobianFromAD(adRes, adDirOffset);

			return retCode;
		}
#else
		// Compute Jacobian via AD

		// Copy over state vector to AD state vector (without changing directional values to keep seed vectors)
		// and initalize residuals with zero (also resetting directional values)
		ad::copyToAd(y, adY, numDofs());
		// @todo Check if this is necessary
		ad::resetAd(adRes, numDofs());

		// Evaluate with AD enabled
		int retCode = 0;
		if (paramSensitivity)
			retCode = residualImpl<active, active, active, false>(t, secIdx, timeFactor, adY, yDot, adRes);
		else
			retCode = residualImpl<active, active, double, false>(static_cast<double>(t), secIdx, static_cast<double>(timeFactor), adY, yDot, adRes);

		// Only do comparison if we have a residuals vector (which is not always the case)
		if (res)
		{
			// Evaluate with analytical Jacobian which is stored in the band matrices
			retCode = residualImpl<double, double, double, true>(static_cast<double>(t), secIdx, static_cast<double>(timeFactor), y, yDot, res);

			// Compare AD with anaytic Jacobian
			checkAnalyticJacobianAgainstAd(adRes, adDirOffset);
		}

		// Extract Jacobian
		extractJacobianFromAD(adRes, adDirOffset);

		return retCode;
#endif
	}
	else
	{
		if (paramSensitivity)
		{
			// Initalize residuals with zero
			// @todo Check if this is necessary
			ad::resetAd(adRes, numDofs());

			const int retCode = residualImpl<double, active, active, false>(t, secIdx, timeFactor, y, yDot, adRes);

			// Copy AD residuals to original residuals vector
			if (res)
				ad::copyFromAd(adRes, res, numDofs());

			return retCode;
		}
		else
			return residualImpl<double, double, double, false>(static_cast<double>(t), secIdx, static_cast<double>(timeFactor), y, yDot, res);
	}
}

int CSTRModel::residualWithJacobian(const active& t, unsigned int secIdx, const active& timeFactor, double const* const y, double const* const yDot, double* const res, active* const adRes, active* const adY, unsigned int adDirOffset)
{
	return residual(t, secIdx, timeFactor, y, yDot, res, adRes, adY, adDirOffset, true, false);
}

int CSTRModel::residualSensFwdAdOnly(const active& t, unsigned int secIdx, const active& timeFactor,
	double const* const y, double const* const yDot, active* const adRes)
{
	// Evaluate residual for all parameters using AD in vector mode
	return residualImpl<double, active, active, false>(t, secIdx, timeFactor, y, yDot, adRes);
}

int CSTRModel::residualSensFwdCombine(const active& t, unsigned int secIdx, const active& timeFactor, double const* const y, double const* const yDot, 
	const std::vector<const double*>& yS, const std::vector<const double*>& ySdot, const std::vector<double*>& resS, active const* adRes, 
	double* const tmp1, double* const tmp2, double* const tmp3)
{
	for (unsigned int param = 0; param < yS.size(); param++)
	{
		// Directional derivative (dF / dy) * s
		multiplyWithJacobian(static_cast<double>(t), secIdx, static_cast<double>(timeFactor), y, yDot, yS[param], 1.0, 0.0, tmp1);

		// Directional derivative (dF / dyDot) * sDot
		multiplyWithDerivativeJacobian(static_cast<double>(t), secIdx, static_cast<double>(timeFactor), y, yDot, ySdot[param], tmp2);

		// Complete sens residual is the sum:
		double* const ptrResS = resS[param];
		for (unsigned int i = 0; i < numDofs(); ++i)
		{
			ptrResS[i] = tmp1[i] + tmp2[i] + adRes[i].getADValue(param);
		}
	}
	return 0;
}

int CSTRModel::residualSensFwdWithJacobian(const active& t, unsigned int secIdx, const active& timeFactor, double const* const y,
	double const* const yDot, active* const adRes, active* const adY, unsigned int adDirOffset)
{
	// Evaluate residual for all parameters using AD in vector mode and at the same time update the 
	// Jacobian (in one AD run, if analytic Jacobians are disabled)
	return residual(t, secIdx, timeFactor, y, yDot, nullptr, adRes, adY, adDirOffset, true, true);
}

void CSTRModel::consistentInitialSensitivity(const active& t, unsigned int secIdx, const active& timeFactor, double const* vecStateY, double const* vecStateYdot,
	std::vector<double*>& vecSensY, std::vector<double*>& vecSensYdot, active const* const adRes)
{
	for (unsigned int param = 0; param < vecSensY.size(); ++param)
	{
		double* const sensY = vecSensY[param];
		double* const sensYdot = vecSensYdot[param];

		// Calculate -(dF / dY) * s - (dF / dP)
		multiplyWithJacobian(static_cast<double>(t), secIdx, static_cast<double>(timeFactor), vecStateY, vecStateYdot, sensY, -1.0, 0.0, sensYdot);

		for (unsigned int i = _nComp; i < numDofs(); ++i)
			sensYdot[i] -= adRes[i].getADValue(param);

		// Solve for \dot{s}
		_jac.setAll(0.0);
		addTimeDerivativeJacobian(static_cast<double>(t), static_cast<double>(timeFactor), vecStateY, vecStateYdot, _jac);
		_jac.factorize();
		_jac.solve(sensYdot + _nComp);
	}
}

void CSTRModel::multiplyWithJacobian(double t, unsigned int secIdx, double timeFactor, double const* const y, double const* const yDot, double const* yS, double alpha, double beta, double* ret)
{
	const double flowIn = static_cast<double>(_flowRateIn);
	double* const resTank = ret + _nComp;

	// Inlet DOFs
	for (unsigned int i = 0; i < _nComp; ++i)
	{
		ret[i] = alpha * yS[i] + beta * ret[i];
	}

	// Multiply with main body Jacobian: dRes / dy
	_jac.multiplyVector(yS + _nComp, alpha, beta, resTank);

	// Map inlet DOFs to the tank (tank cells)
	for (unsigned int i = 0; i < _nComp; ++i)
	{
		resTank[i] -= alpha * flowIn * yS[i];
	}
}

void CSTRModel::multiplyWithDerivativeJacobian(double t, unsigned int secIdx, double timeFactor, double const* const y, double const* const yDot, double const* sDot, double* ret)
{
	_jac.setAll(0.0);
	addTimeDerivativeJacobian(t, timeFactor, y, yDot, _jac);

	// Handle inlet DOFs (all algebraic)
	std::fill_n(ret, _nComp, 0.0);
	// Multiply main body
	_jac.multiplyVector(sDot + _nComp, ret + _nComp);
}

int CSTRModel::linearSolve(double t, double timeFactor, double alpha, double tol, double* const rhs, double const* const weight,
	double const* const y, double const* const yDot, double const* const res)
{
	const double flowIn = static_cast<double>(_flowRateIn);

	// Handle inlet equations by backsubstitution
	for (unsigned int i = 0; i < _nComp; i++)
	{
		rhs[i + _nComp] += flowIn * rhs[i];
	}

	bool success = true;
	if (_factorizeJac)
	{
		// Factorization is necessary
		_factorizeJac = false;
		_jacFact.copyFrom(_jac);

		addTimeDerivativeJacobian(t, timeFactor, y, yDot, _jac);
		success = _jacFact.factorize();
	}
	success = success && _jacFact.solve(rhs + _nComp);

	// Return 0 on success and 1 on failure
	return success ? 0 : 1;
}

template <typename MatrixType>
void CSTRModel::addTimeDerivativeJacobian(double t, double timeFactor, double const* y, double const* yDot, MatrixType& mat)
{
	double const* const c = y + _nComp;
	double const* const q = y + 2 * _nComp;
	const double v = y[2 * _nComp + _strideBound];
	const double invBeta = 1.0 / static_cast<double>(_porosity) - 1.0;
	const double vInvBeta = timeFactor * v * invBeta;
	const double timeV = timeFactor * v;

	// Assemble Jacobian: dRes / dyDot

	// Concentrations: \dot{V} * (c_i + invBeta * sum_j q_{i,j}) + V * (\dot{c}_i + invBeta * sum_j \dot{q}_{i,j}) - c_{in,i} * F_in + c_i * F_out == 0
	for (unsigned int i = 0; i < _nComp; i++)
	{
		mat.native(i, i) = timeV;

		double qSum = 0.0;
		double const* const qi = q + _boundOffset[i];
		const unsigned int localOffset = _nComp + _boundOffset[i];
		for (unsigned int j = 0; j < _nBound[i]; ++j)
		{
			mat.native(i, localOffset + j) = vInvBeta;
			// + _nComp: Moves over liquid phase components
			// + _boundOffset[i]: Moves over bound states of previous components
			// + j: Moves to current bound state j of component i

			qSum += qi[j];
		}
		mat.native(i, _nComp + _strideBound) = timeFactor * (c[i] + invBeta * qSum);
	}

	// Bound states
	_binding->jacobianAddDiscretized(timeFactor, mat.row(_nComp));

	// Volume: \dot{V} - F_{in} + F_{out} + F_{filter} == 0
	mat.native(_nComp + _strideBound, _nComp + _strideBound) = timeFactor;
}

/**
 * @brief Extracts the system Jacobian from AD seed vectors
 * @param [in] adRes Residual vector of AD datatypes with band compressed seed vectors
 * @param [in] adDirOffset Number of AD directions used for non-Jacobian purposes (e.g., parameter sensitivities)
 */
void CSTRModel::extractJacobianFromAD(active const* const adRes, unsigned int adDirOffset)
{
	ad::extractDenseJacobianFromAd(adRes + _nComp, adDirOffset, _jac);
}

#ifdef CADET_CHECK_ANALYTIC_JACOBIAN

/**
 * @brief Compares the analytical Jacobian with a Jacobian derived by AD
 * @details The analytical Jacobian is assumed to be stored in the dense matrix.
 * @param [in] adRes Residual vector of AD datatypes with band compressed seed vectors
 * @param [in] adDirOffset Number of AD directions used for non-Jacobian purposes (e.g., parameter sensitivities)
 */
void CSTRModel::checkAnalyticJacobianAgainstAd(active const* const adRes, unsigned int adDirOffset) const
{
	const double diff = ad::compareDenseJacobianWithAd(adRes + _nComp, adDirOffset, _jac);
	LOG(Debug) << "AD dir offset: " << adDirOffset << " diff: " << diff;
}

#endif

}  // namespace model

}  // namespace cadet