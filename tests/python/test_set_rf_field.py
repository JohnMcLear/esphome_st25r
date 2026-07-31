"""
Unit tests for the st25r.set_rf_field / st25r300.set_rf_field action schema.

Covers the config validation in:
  components/st25r/__init__.py
  components/st25r300/__init__.py

Run with:
  pytest tests/python/ -v
"""

import pytest
import esphome.config_validation as cv  # ESPHome must be installed: pip install esphome


# ── Inline mirror of the action schema ───────────────────────────────────────
# Copied from the register_action() call in components/st25r/__init__.py, minus
# the cv.GenerateID()/cv.use_id() entry, which needs a live ESPHome CORE context
# to resolve and is not what these tests are about. The st25r300 twin uses an
# identical shape, so this covers both.
SET_RF_FIELD_SCHEMA = cv.maybe_simple_value(
    {
        cv.Required("field_on"): cv.boolean,
    },
    key="field_on",
)


class TestShorthandForm:
    """`st25r.set_rf_field: false` — the form that appears in real configs.

    This is the one worth pinning down. maybe_simple_value() is what allows the
    bare-value spelling, and a refactor that drops it would still validate the
    verbose form while silently breaking every config using the short one.
    """

    def test_bare_false(self):
        assert SET_RF_FIELD_SCHEMA(False) == {"field_on": False}

    def test_bare_true(self):
        assert SET_RF_FIELD_SCHEMA(True) == {"field_on": True}

    # ESPHome accepts YAML 1.1 boolean spellings. `off` matters in particular:
    # a plain YAML `off` is exactly how someone would write "drop the field".
    @pytest.mark.parametrize("value", ["on", "yes", "true", "TRUE"])
    def test_truthy_spellings(self, value):
        assert SET_RF_FIELD_SCHEMA(value) == {"field_on": True}

    @pytest.mark.parametrize("value", ["off", "no", "false", "FALSE"])
    def test_falsey_spellings(self, value):
        assert SET_RF_FIELD_SCHEMA(value) == {"field_on": False}


class TestVerboseForm:
    """`st25r.set_rf_field: {id: reader, field_on: false}`."""

    def test_explicit_mapping(self):
        assert SET_RF_FIELD_SCHEMA({"field_on": False}) == {"field_on": False}

    def test_field_on_is_required(self):
        # An empty mapping must not quietly default to on or off -- silently
        # picking a direction here would be worse than failing the build.
        with pytest.raises(cv.Invalid):
            SET_RF_FIELD_SCHEMA({})


class TestRejectsGarbage:
    @pytest.mark.parametrize("value", ["banana", 3.7, [], "onn"])
    def test_non_boolean_rejected(self, value):
        with pytest.raises(cv.Invalid):
            SET_RF_FIELD_SCHEMA(value)
