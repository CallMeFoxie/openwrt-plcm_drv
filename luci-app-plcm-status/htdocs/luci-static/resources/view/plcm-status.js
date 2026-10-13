'use strict';
'require view';
'require form';
'require uci';

function addWidgetOptions(section, optionName, title) {
	var widget = section.option(form.ListValue, optionName, title);
	widget.modalonly = true;
	widget.rmempty = false;
	widget.default = 'none';
	widget.value('none', _('Empty'));
	widget.value('text', _('Static text'));
	widget.value('hostname', _('Hostname'));
	widget.value('clock', _('Clock'));
	widget.value('date', _('Date'));
	widget.value('uptime', _('Uptime'));
	widget.value('cpu', _('CPU usage'));
	widget.value('ram', _('Memory usage'));
	widget.value('load', _('System load'));
	widget.value('ipv4', _('IPv4 address'));
	widget.value('rx', _('Receive rate'));
	widget.value('tx', _('Transmit rate'));

	var argument = section.option(form.Value, optionName + '_arg', _('Argument'));
	argument.modalonly = true;
	argument.rmempty = true;
	argument.description = _('Static text, strftime format, or network interface, depending on the selected widget.');
	argument.depends(optionName, 'text');
	argument.depends(optionName, 'clock');
	argument.depends(optionName, 'date');
	argument.depends(optionName, 'ipv4');
	argument.depends(optionName, 'rx');
	argument.depends(optionName, 'tx');
}

return view.extend({
	load: function() {
		return uci.load('plcm-status');
	},

	render: function() {
		var map = new form.Map('plcm-status', _('PLCM Display'));
		var section = map.section(form.NamedSection, 'main', 'status', _('General settings'));
		section.anonymous = true;

		var option = section.option(form.Value, 'device', _('Device'));
		option.default = '/dev/plcm_drv';
		option.rmempty = false;

		option = section.option(form.Value, 'refresh', _('Refresh interval'));
		option.datatype = 'uinteger';
		option.default = '1';
		option.rmempty = false;
		option.description = _('Number of seconds between display and metric updates.');

		section = map.section(form.GridSection, 'page', _('Pages'));
		section.anonymous = true;
		section.addremove = true;
		section.sortable = true;
		section.nodescriptions = true;
		section.description = _('Pages rotate in this order. A single widget uses all 20 columns; two widgets use 10 columns each.');

		option = section.option(form.Flag, 'enabled', _('Enabled'));
		option.default = option.enabled;
		option.rmempty = false;
		option.editable = true;

		option = section.option(form.Value, 'name', _('Name'));
		option.default = _('Page');
		option.rmempty = false;

		option = section.option(form.Value, 'duration', _('Duration'));
		option.datatype = 'uinteger';
		option.default = '5';
		option.rmempty = false;
		option.description = _('Seconds before advancing to the next enabled page.');

		addWidgetOptions(section, 'line1_left', _('Line 1 left'));
		addWidgetOptions(section, 'line1_right', _('Line 1 right'));
		addWidgetOptions(section, 'line2_left', _('Line 2 left'));
		addWidgetOptions(section, 'line2_right', _('Line 2 right'));

		return map.render();
	}
});