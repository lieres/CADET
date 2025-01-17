// =============================================================================
//  CADET
//  
//  Copyright © 2008-present: The CADET-Core Authors
//            Please see the AUTHORS.md file.
//  
//  All rights reserved. This program and the accompanying materials
//  are made available under the terms of the GNU Public License v3.0 (or, at
//  your option, any later version) which accompanies this distribution, and
//  is available at http://www.gnu.org/licenses/gpl.html
// =============================================================================

#include "cadet/Model.hpp"
#include "cadet/ParameterId.hpp"
#include "cadet/ParameterProvider.hpp"

#include "ConfigurationHelper.hpp"

namespace
{
	class DummyConfigHelper : public cadet::IConfigHelper
	{
	public:

		DummyConfigHelper() { }

		virtual cadet::IInletProfile* createInletProfile(const std::string& type) const { return nullptr; }
		virtual cadet::model::IBindingModel* createBindingModel(const std::string& name) const { return nullptr; }
		virtual bool isValidBindingModel(const std::string& name) const { return false; }
		virtual cadet::IExternalFunction* createExternalFunction(const std::string& type) const { return nullptr; }
		virtual cadet::model::IDynamicReactionModel* createDynamicReactionModel(const std::string& name) const { return nullptr; }
		virtual bool isValidDynamicReactionModel(const std::string& name) const { return false; }
		virtual cadet::model::IParameterStateDependence* createParameterStateDependence(const std::string& name) const { return nullptr; }
		virtual bool isValidParameterStateDependence(const std::string& name) const { return false; }
		virtual cadet::model::IParameterParameterDependence* createParameterParameterDependence(const std::string& name) const { return nullptr; }
		virtual bool isValidParameterParameterDependence(const std::string& name) const { return false; }
	};

	class DummyModel : public cadet::IModel
	{
	public:
		DummyModel() { }
		virtual ~DummyModel() CADET_NOEXCEPT { }

		virtual cadet::UnitOpIdx unitOperationId() const CADET_NOEXCEPT { return 0; };
		virtual const char* unitOperationName() const CADET_NOEXCEPT { return "DUMMY"; }

		virtual bool setParameter(const cadet::ParameterId& pId, int value) { return false; }
		virtual bool setParameter(const cadet::ParameterId& pId, double value) { return false; }
		virtual bool setParameter(const cadet::ParameterId& pId, bool value) { return false; }

		virtual bool hasParameter(const cadet::ParameterId& pId) const { return false; }

		virtual std::unordered_map<cadet::ParameterId, double> getAllParameterValues() const { return std::unordered_map<cadet::ParameterId, double>(); }

		virtual double getParameterDouble(const cadet::ParameterId& pId) const { return 0.0; }

		virtual void useAnalyticJacobian(const bool analyticJac) { }

#ifdef CADET_BENCHMARK_MODE
		virtual std::vector<double> benchmarkTimings() const { return std::vector<double>(0); }
		virtual char const* const* benchmarkDescriptions() const { return nullptr; }
#endif
	};

	class DummyParameterProvider : public cadet::IParameterProvider
	{
	public:

		DummyParameterProvider() { }
		virtual ~DummyParameterProvider() CADET_NOEXCEPT { }

		virtual double getDouble(const std::string& paramName) { return 0.0; }
		virtual int getInt(const std::string& paramName) { return 0; }
		virtual uint64_t getUint64(const std::string& paramName) { return 0; }
		virtual bool getBool(const std::string& paramName) { return false; }
		virtual std::string getString(const std::string& paramName) { return ""; }

		virtual std::vector<double> getDoubleArray(const std::string& paramName) { return std::vector<double>(); }
		virtual std::vector<int> getIntArray(const std::string& paramName) { return std::vector<int>(); }
		virtual std::vector<uint64_t> getUint64Array(const std::string& paramName) { return std::vector<uint64_t>(); }
		virtual std::vector<bool> getBoolArray(const std::string& paramName) { return std::vector<bool>(); }
		virtual std::vector<std::string> getStringArray(const std::string& paramName) { return std::vector<std::string>(); }

		virtual bool exists(const std::string& paramName) { return false; }
		virtual bool isArray(const std::string& paramName) { return false; }

		virtual std::size_t numElements(const std::string& paramName) { return 0; }

		virtual void pushScope(const std::string& scope) { }
		virtual void popScope() { }
	};
}
