/*
 * helpers.c  --  Voltage/Current Regulator framework helper functions.
 *
 * Copyright 2007, 2008 Wolfson Microelectronics PLC.
 * Copyright 2008 SlimLogic Ltd.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/module.h>

#include "internal.h"

/**
 * regulator_is_enabled_regmap - standard is_enabled() for regmap users
 *
 * @rdev: regulator to operate on
 *
 * Regulators that use regmap for their register I/O can set the
 * enable_reg and enable_mask fields in their descriptor and then use
 * this as their is_enabled operation, saving some code.
 */
int regulator_is_enabled_regmap(struct regulator_dev *rdev)
{
	unsigned int val;
	int ret;

	ret = regmap_read(rdev->regmap, rdev->desc->enable_reg, &val);
	if (ret != 0)
		return ret;

	val &= rdev->desc->enable_mask;

	if (rdev->desc->enable_is_inverted) {
		if (rdev->desc->enable_val)
			return val != rdev->desc->enable_val;
		return val == 0;
	} else {
		if (rdev->desc->enable_val)
			return val == rdev->desc->enable_val;
		return val != 0;
	}
}
EXPORT_SYMBOL_GPL(regulator_is_enabled_regmap);

/**
 * regulator_enable_regmap - standard enable() for regmap users
 *
 * @rdev: regulator to operate on
 *
 * Regulators that use regmap for their register I/O can set the
 * enable_reg and enable_mask fields in their descriptor and then use
 * this as their enable() operation, saving some code.
 */
int regulator_enable_regmap(struct regulator_dev *rdev)
{
	unsigned int val;

	if (rdev->desc->enable_is_inverted) {
		val = rdev->desc->disable_val;
	} else {
		val = rdev->desc->enable_val;
		if (!val)
			val = rdev->desc->enable_mask;
	}

	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  rdev->desc->enable_mask, val);
}
EXPORT_SYMBOL_GPL(regulator_enable_regmap);

/**
 * regulator_disable_regmap - standard disable() for regmap users
 *
 * @rdev: regulator to operate on
 *
 * Regulators that use regmap for their register I/O can set the
 * enable_reg and enable_mask fields in their descriptor and then use
 * this as their disable() operation, saving some code.
 */
int regulator_disable_regmap(struct regulator_dev *rdev)
{
	unsigned int val;

	if (rdev->desc->enable_is_inverted) {
		val = rdev->desc->enable_val;
		if (!val)
			val = rdev->desc->enable_mask;
	} else {
		val = rdev->desc->disable_val;
	}

	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  rdev->desc->enable_mask, val);
}
EXPORT_SYMBOL_GPL(regulator_disable_regmap);

static int regulator_range_selector_to_index(struct regulator_dev *rdev,
					     unsigned int rval)
{
	int i;

	if (!rdev->desc->linear_range_selectors)
		return -EINVAL;

	rval &= rdev->desc->vsel_range_mask;

	for (i = 0; i < rdev->desc->n_linear_ranges; i++) {
		if (rdev->desc->linear_range_selectors[i] == rval)
			return i;
	}
	return -EINVAL;
}

/**
 * regulator_get_voltage_sel_pickable_regmap - pickable range get_voltage_sel
 *
 * @rdev: regulator to operate on
 *
 * Regulators that use regmap for their register I/O and use pickable
 * ranges can set the vsel_reg, vsel_mask, vsel_range_reg and vsel_range_mask
 * fields in their descriptor and then use this as their get_voltage_vsel
 * operation, saving some code.
 */
int regulator_get_voltage_sel_pickable_regmap(struct regulator_dev *rdev)
{
	unsigned int r_val;
	int range;
	unsigned int val;
	int ret, i;
	unsigned int voltages_in_range = 0;

	if (!rdev->desc->linear_ranges)
		return -EINVAL;

	ret = regmap_read(rdev->regmap, rdev->desc->vsel_reg, &val);
	if (ret != 0)
		return ret;

	ret = regmap_read(rdev->regmap, rdev->desc->vsel_range_reg, &r_val);
	if (ret != 0)
		return ret;

	val &= rdev->desc->vsel_mask;
	val >>= ffs(rdev->desc->vsel_mask) - 1;

	range = regulator_range_selector_to_index(rdev, r_val);
	if (range < 0)
		return -EINVAL;

	for (i = 0; i < range; i++)
		voltages_in_range += (rdev->desc->linear_ranges[i].max_sel -
				     rdev->desc->linear_ranges[i].min_sel) + 1;

	return val + voltages_in_range;
}
EXPORT_SYMBOL_GPL(regulator_get_voltage_sel_pickable_regmap);

/**
 * regulator_set_voltage_sel_pickable_regmap - pickable range set_voltage_sel
 *
 * @rdev: regulator to operate on
 * @sel: Selector to set
 *
 * Regulators that use regmap for their register I/O and use pickable
 * ranges can set the vsel_reg, vsel_mask, vsel_range_reg and vsel_range_mask
 * fields in their descriptor and then use this as their set_voltage_vsel
 * operation, saving some code.
 */
int regulator_set_voltage_sel_pickable_regmap(struct regulator_dev *rdev,
					      unsigned int sel)
{
	unsigned int range;
	int ret, i;
	unsigned int voltages_in_range = 0;

	for (i = 0; i < rdev->desc->n_linear_ranges; i++) {
		voltages_in_range = (rdev->desc->linear_ranges[i].max_sel -
				     rdev->desc->linear_ranges[i].min_sel) + 1;
		if (sel < voltages_in_range)
			break;
		sel -= voltages_in_range;
	}

	if (i == rdev->desc->n_linear_ranges)
		return -EINVAL;

	sel <<= ffs(rdev->desc->vsel_mask) - 1;
	sel += rdev->desc->linear_ranges[i].min_sel;

	range = rdev->desc->linear_range_selectors[i];

	if (rdev->desc->vsel_reg == rdev->desc->vsel_range_reg) {
		ret = regmap_update_bits(rdev->regmap,
					 rdev->desc->vsel_reg,
					 rdev->desc->vsel_range_mask |
					 rdev->desc->vsel_mask, sel | range);
	} else {
		ret = regmap_update_bits(rdev->regmap,
					 rdev->desc->vsel_range_reg,
					 rdev->desc->vsel_range_mask, range);
		if (ret)
			return ret;

		ret = regmap_update_bits(rdev->regmap, rdev->desc->vsel_reg,
				  rdev->desc->vsel_mask, sel);
	}

	if (ret)
		return ret;

	if (rdev->desc->apply_bit)
		ret = regmap_update_bits(rdev->regmap, rdev->desc->apply_reg,
					 rdev->desc->apply_bit,
					 rdev->desc->apply_bit);
	return ret;
}
EXPORT_SYMBOL_GPL(regulator_set_voltage_sel_pickable_regmap);

/**
 * regulator_get_voltage_sel_regmap - standard get_voltage_sel for regmap users
 *
 * @rdev: regulator to operate on
 *
 * Regulators that use regmap for their register I/O can set the
 * vsel_reg and vsel_mask fields in their descriptor and then use this
 * as their get_voltage_vsel operation, saving some code.
 */
int regulator_get_voltage_sel_regmap(struct regulator_dev *rdev)
{
	unsigned int val;
	int ret;

	ret = regmap_read(rdev->regmap, rdev->desc->vsel_reg, &val);
	if (ret != 0)
		return ret;

	val &= rdev->desc->vsel_mask;
	val >>= ffs(rdev->desc->vsel_mask) - 1;

	return val;
}
EXPORT_SYMBOL_GPL(regulator_get_voltage_sel_regmap);

/**
 * regulator_set_voltage_sel_regmap - standard set_voltage_sel for regmap users
 *
 * @rdev: regulator to operate on
 * @sel: Selector to set
 *
 * Regulators that use regmap for their register I/O can set the
 * vsel_reg and vsel_mask fields in their descriptor and then use this
 * as their set_voltage_vsel operation, saving some code.
 */
int regulator_set_voltage_sel_regmap(struct regulator_dev *rdev, unsigned sel)
{
	int ret;

	sel <<= ffs(rdev->desc->vsel_mask) - 1;

	ret = regmap_update_bits(rdev->regmap, rdev->desc->vsel_reg,
				  rdev->desc->vsel_mask, sel);
	if (ret)
		return ret;

	if (rdev->desc->apply_bit)
		ret = regmap_update_bits(rdev->regmap, rdev->desc->apply_reg,
					 rdev->desc->apply_bit,
					 rdev->desc->apply_bit);
	return ret;
}
EXPORT_SYMBOL_GPL(regulator_set_voltage_sel_regmap);

/**
 * regulator_map_voltage_iterate - map_voltage() based on list_voltage()
 *
 * @rdev: Regulator to operate on
 * @min_uV: Lower bound for voltage
 * @max_uV: Upper bound for voltage
 *
 * Drivers implementing set_voltage_sel() and list_voltage() can use
 * this as their map_voltage() operation.  It will find a suitable
 * voltage by calling list_voltage() until it gets something in bounds
 * for the requested voltages.
 */
int regulator_map_voltage_iterate(struct regulator_dev *rdev,
				  int min_uV, int max_uV)
{
	int best_val = INT_MAX;
	int selector = 0;
	int i, ret;

	/* Find the smallest voltage that falls within the specified
	 * range.
	 */
	for (i = 0; i < rdev->desc->n_voltages; i++) {
		ret = rdev->desc->ops->list_voltage(rdev, i);
		if (ret < 0)
			continue;

		if (ret < best_val && ret >= min_uV && ret <= max_uV) {
			best_val = ret;
			selector = i;
		}
	}

	if (best_val != INT_MAX)
		return selector;
	else
		return -EINVAL;
}
EXPORT_SYMBOL_GPL(regulator_map_voltage_iterate);

/**
 * regulator_map_voltage_ascend - map_voltage() for ascendant voltage list
 *
 * @rdev: Regulator to operate on
 * @min_uV: Lower bound for voltage
 * @max_uV: Upper bound for voltage
 *
 * Drivers that have ascendant voltage list can use this as their
 * map_voltage() operation.
 */
int regulator_map_voltage_ascend(struct regulator_dev *rdev,
				 int min_uV, int max_uV)
{
	int i, ret;

	for (i = 0; i < rdev->desc->n_voltages; i++) {
		ret = rdev->desc->ops->list_voltage(rdev, i);
		if (ret < 0)
			continue;

		if (ret > max_uV)
			break;

		if (ret >= min_uV && ret <= max_uV)
			return i;
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(regulator_map_voltage_ascend);

/**
 * regulator_map_voltage_linear - map_voltage() for simple linear mappings
 *
 * @rdev: Regulator to operate on
 * @min_uV: Lower bound for voltage
 * @max_uV: Upper bound for voltage
 *
 * Drivers providing min_uV and uV_step in their regulator_desc can
 * use this as their map_voltage() operation.
 */
int regulator_map_voltage_linear(struct regulator_dev *rdev,
				 int min_uV, int max_uV)
{
	int ret, voltage;

	/* Allow uV_step to be 0 for fixed voltage */
	if (rdev->desc->n_voltages == 1 && rdev->desc->uV_step == 0) {
		if (min_uV <= rdev->desc->min_uV && rdev->desc->min_uV <= max_uV)
			return 0;
		else
			return -EINVAL;
	}

	if (!rdev->desc->uV_step) {
		BUG_ON(!rdev->desc->uV_step);
		return -EINVAL;
	}

	if (min_uV < rdev->desc->min_uV)
		min_uV = rdev->desc->min_uV;

	ret = DIV_ROUND_UP(min_uV - rdev->desc->min_uV, rdev->desc->uV_step);
	if (ret < 0)
		return ret;

	ret += rdev->desc->linear_min_sel;

	/* Map back into a voltage to verify we're still in bounds */
	voltage = rdev->desc->ops->list_voltage(rdev, ret);
	if (voltage < min_uV || voltage > max_uV)
		return -EINVAL;

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_map_voltage_linear);

/**
 * regulator_map_voltage_linear_range - map_voltage() for multiple linear ranges
 *
 * @rdev: Regulator to operate on
 * @min_uV: Lower bound for voltage
 * @max_uV: Upper bound for voltage
 *
 * Drivers providing linear_ranges in their descriptor can use this as
 * their map_voltage() callback.
 */
int regulator_map_voltage_linear_range(struct regulator_dev *rdev,
				       int min_uV, int max_uV)
{
	const struct regulator_linear_range *range;
	int ret = -EINVAL;
	int voltage, i;

	if (!rdev->desc->n_linear_ranges) {
		BUG_ON(!rdev->desc->n_linear_ranges);
		return -EINVAL;
	}

	for (i = 0; i < rdev->desc->n_linear_ranges; i++) {
		int linear_max_uV;

		range = &rdev->desc->linear_ranges[i];
		linear_max_uV = range->min_uV +
			(range->max_sel - range->min_sel) * range->uV_step;

		if (!(min_uV <= linear_max_uV && max_uV >= range->min_uV))
			continue;

		if (min_uV <= range->min_uV)
			min_uV = range->min_uV;

		/* range->uV_step == 0 means fixed voltage range */
		if (range->uV_step == 0) {
			ret = 0;
		} else {
			ret = DIV_ROUND_UP(min_uV - range->min_uV,
					   range->uV_step);
			if (ret < 0)
				return ret;
		}

		ret += range->min_sel;

		/*
		 * Map back into a voltage to verify we're still in bounds.
		 * If we are not, then continue checking rest of the ranges.
		 */
		voltage = rdev->desc->ops->list_voltage(rdev, ret);
		if (voltage >= min_uV && voltage <= max_uV)
			break;
	}

	if (i == rdev->desc->n_linear_ranges)
		return -EINVAL;

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_map_voltage_linear_range);

/**
 * regulator_map_voltage_pickable_linear_range - map_voltage, pickable ranges
 *
 * @rdev: Regulator to operate on
 * @min_uV: Lower bound for voltage
 * @max_uV: Upper bound for voltage
 *
 * Drivers providing pickable linear_ranges in their descriptor can use
 * this as their map_voltage() callback.
 */
int regulator_map_voltage_pickable_linear_range(struct regulator_dev *rdev,
						int min_uV, int max_uV)
{
	const struct regulator_linear_range *range;
	int ret = -EINVAL;
	int voltage, i;
	unsigned int selector = 0;

	if (!rdev->desc->n_linear_ranges) {
		BUG_ON(!rdev->desc->n_linear_ranges);
		return -EINVAL;
	}

	for (i = 0; i < rdev->desc->n_linear_ranges; i++) {
		int linear_max_uV;

		range = &rdev->desc->linear_ranges[i];
		linear_max_uV = range->min_uV +
			(range->max_sel - range->min_sel) * range->uV_step;

		if (!(min_uV <= linear_max_uV && max_uV >= range->min_uV)) {
			selector += (range->max_sel - range->min_sel + 1);
			continue;
		}

		if (min_uV <= range->min_uV)
			min_uV = range->min_uV;

		/* range->uV_step == 0 means fixed voltage range */
		if (range->uV_step == 0) {
			ret = 0;
		} else {
			ret = DIV_ROUND_UP(min_uV - range->min_uV,
					   range->uV_step);
			if (ret < 0)
				return ret;
		}

		ret += selector;

		voltage = rdev->desc->ops->list_voltage(rdev, ret);

		/*
		 * Map back into a voltage to verify we're still in bounds.
		 * We may have overlapping voltage ranges. Hence we don't
		 * exit but retry until we have checked all ranges.
		 */
		if (voltage < min_uV || voltage > max_uV)
			selector += (range->max_sel - range->min_sel + 1);
		else
			break;
	}

	if (i == rdev->desc->n_linear_ranges)
		return -EINVAL;

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_map_voltage_pickable_linear_range);

/**
 * regulator_list_voltage_linear - List voltages with simple calculation
 *
 * @rdev: Regulator device
 * @selector: Selector to convert into a voltage
 *
 * Regulators with a simple linear mapping between voltages and
 * selectors can set min_uV and uV_step in the regulator descriptor
 * and then use this function as their list_voltage() operation,
 */
int regulator_list_voltage_linear(struct regulator_dev *rdev,
				  unsigned int selector)
{
	if (selector >= rdev->desc->n_voltages)
		return -EINVAL;
	if (selector < rdev->desc->linear_min_sel)
		return 0;

	selector -= rdev->desc->linear_min_sel;

	return rdev->desc->min_uV + (rdev->desc->uV_step * selector);
}
EXPORT_SYMBOL_GPL(regulator_list_voltage_linear);

/**
 * regulator_list_voltage_pickable_linear_range - pickable range list voltages
 *
 * @rdev: Regulator device
 * @selector: Selector to convert into a voltage
 *
 * list_voltage() operation, intended to be used by drivers utilizing pickable
 * ranges helpers.
 */
int regulator_list_voltage_pickable_linear_range(struct regulator_dev *rdev,
						 unsigned int selector)
{
	const struct regulator_linear_range *range;
	int i;
	unsigned int all_sels = 0;

	if (!rdev->desc->n_linear_ranges) {
		BUG_ON(!rdev->desc->n_linear_ranges);
		return -EINVAL;
	}

	for (i = 0; i < rdev->desc->n_linear_ranges; i++) {
		unsigned int sels_in_range;

		range = &rdev->desc->linear_ranges[i];

		sels_in_range = range->max_sel - range->min_sel;

		if (all_sels + sels_in_range >= selector) {
			selector -= all_sels;
			return range->min_uV + (range->uV_step * selector);
		}

		all_sels += (sels_in_range + 1);
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(regulator_list_voltage_pickable_linear_range);

/**
 * regulator_list_voltage_linear_range - List voltages for linear ranges
 *
 * @rdev: Regulator device
 * @selector: Selector to convert into a voltage
 *
 * Regulators with a series of simple linear mappings between voltages
 * and selectors can set linear_ranges in the regulator descriptor and
 * then use this function as their list_voltage() operation,
 */
int regulator_list_voltage_linear_range(struct regulator_dev *rdev,
					unsigned int selector)
{
	const struct regulator_linear_range *range;
	int i;

	if (!rdev->desc->n_linear_ranges) {
		BUG_ON(!rdev->desc->n_linear_ranges);
		return -EINVAL;
	}

	for (i = 0; i < rdev->desc->n_linear_ranges; i++) {
		range = &rdev->desc->linear_ranges[i];

		if (!(selector >= range->min_sel &&
		      selector <= range->max_sel))
			continue;

		selector -= range->min_sel;

		return range->min_uV + (range->uV_step * selector);
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(regulator_list_voltage_linear_range);

/**
 * regulator_list_voltage_table - List voltages with table based mapping
 *
 * @rdev: Regulator device
 * @selector: Selector to convert into a voltage
 *
 * Regulators with table based mapping between voltages and
 * selectors can set volt_table in the regulator descriptor
 * and then use this function as their list_voltage() operation.
 */
int regulator_list_voltage_table(struct regulator_dev *rdev,
				 unsigned int selector)
{
	if (!rdev->desc->volt_table) {
		BUG_ON(!rdev->desc->volt_table);
		return -EINVAL;
	}

	if (selector >= rdev->desc->n_voltages)
		return -EINVAL;

	return rdev->desc->volt_table[selector];
}
EXPORT_SYMBOL_GPL(regulator_list_voltage_table);

/**
 * regulator_set_bypass_regmap - Default set_bypass() using regmap
 *
 * @rdev: device to operate on.
 * @enable: state to set.
 */
int regulator_set_bypass_regmap(struct regulator_dev *rdev, bool enable)
{
	unsigned int val;

	if (enable) {
		val = rdev->desc->bypass_val_on;
		if (!val)
			val = rdev->desc->bypass_mask;
	} else {
		val = rdev->desc->bypass_val_off;
	}

	return regmap_update_bits(rdev->regmap, rdev->desc->bypass_reg,
				  rdev->desc->bypass_mask, val);
}
EXPORT_SYMBOL_GPL(regulator_set_bypass_regmap);

/**
 * regulator_set_soft_start_regmap - Default set_soft_start() using regmap
 *
 * @rdev: device to operate on.
 */
int regulator_set_soft_start_regmap(struct regulator_dev *rdev)
{
	unsigned int val;

	val = rdev->desc->soft_start_val_on;
	if (!val)
		val = rdev->desc->soft_start_mask;

	return regmap_update_bits(rdev->regmap, rdev->desc->soft_start_reg,
				  rdev->desc->soft_start_mask, val);
}
EXPORT_SYMBOL_GPL(regulator_set_soft_start_regmap);

/**
 * regulator_set_pull_down_regmap - Default set_pull_down() using regmap
 *
 * @rdev: device to operate on.
 */
int regulator_set_pull_down_regmap(struct regulator_dev *rdev)
{
	unsigned int val;

	val = rdev->desc->pull_down_val_on;
	if (!val)
		val = rdev->desc->pull_down_mask;

	return regmap_update_bits(rdev->regmap, rdev->desc->pull_down_reg,
				  rdev->desc->pull_down_mask, val);
}
EXPORT_SYMBOL_GPL(regulator_set_pull_down_regmap);

/**
 * regulator_get_bypass_regmap - Default get_bypass() using regmap
 *
 * @rdev: device to operate on.
 * @enable: current state.
 */
int regulator_get_bypass_regmap(struct regulator_dev *rdev, bool *enable)
{
	unsigned int val;
	unsigned int val_on = rdev->desc->bypass_val_on;
	int ret;

	ret = regmap_read(rdev->regmap, rdev->desc->bypass_reg, &val);
	if (ret != 0)
		return ret;

	if (!val_on)
		val_on = rdev->desc->bypass_mask;

	*enable = (val & rdev->desc->bypass_mask) == val_on;

	return 0;
}
EXPORT_SYMBOL_GPL(regulator_get_bypass_regmap);

/**
 * regulator_set_active_discharge_regmap - Default set_active_discharge()
 *					   using regmap
 *
 * @rdev: device to operate on.
 * @enable: state to set, 0 to disable and 1 to enable.
 */
int regulator_set_active_discharge_regmap(struct regulator_dev *rdev,
					  bool enable)
{
	unsigned int val;

	if (enable)
		val = rdev->desc->active_discharge_on;
	else
		val = rdev->desc->active_discharge_off;

	return regmap_update_bits(rdev->regmap,
				  rdev->desc->active_discharge_reg,
				  rdev->desc->active_discharge_mask, val);
}
EXPORT_SYMBOL_GPL(regulator_set_active_discharge_regmap);

/**
 * regulator_is_equal - test whether two regulators are the same
 *
 * @reg1: first regulator to operate on
 * @reg2: second regulator to operate on
 */
bool regulator_is_equal(struct regulator *reg1, struct regulator *reg2)
{
	return reg1->rdev == reg2->rdev;
}
EXPORT_SYMBOL_GPL(regulator_is_equal);
