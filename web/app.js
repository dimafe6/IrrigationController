var websocketServerLocation = "ws://" + location.hostname + "/ws";
var ws;
var weekNames = ["Sunday", "Monday", "Tuesday", "Wednesday", "Thuesday", "Friday", "Saturday"];
var periodicityList = { "0": "Hourly", "1": "Every X hours", "2": "Daily", "3": "Every X days", "4": "Weekly", "5": "Monthly", "6": "Once", };
var calendarEvents = {};
var availableSlots;
var calendar;
var currentTime = null;
var settings = { "location": null };
const weatherAPIKey = "4e3720d2b7234ec8b8585710191907";

window.addEventListener('beforeunload', (event) => {
    ws.close();
});

$(document).ready(function () {
    $('#datetimepicker').datetimepicker({
        inline: true,
        sideBySide: true,
        format: 'YYYY-MM-DD HH:mm:ss'
    });

    $('#one-time-datetimepicker').datetimepicker({
        inline: true,
        sideBySide: true,
        format: 'YYYY-MM-DD HH:mm:ss',
        minDate: new Date()
    });

    $(document).on('click', '.save-time-btn', function () {
        var date = $('#datetimepicker').data("DateTimePicker").viewDate();
        setTime(date);
    });

    setInterval(function () {
        if (null !== currentTime) {
            currentTime.add(1, 'seconds');
            $('#current-time').text(currentTime.format('ddd, YYYY-MM-DD HH:mm:ss'));
        }
        $('#one-time-datetimepicker').data("DateTimePicker").minDate(new Date());
    }, 1000);

    moment.updateLocale('en', {
        week: {
            dow: 0,
            doy: 6
        }
    });

    WebSocketBegin(websocketServerLocation);

    $('a[href="#calendar-page"]').click(function () {
        setTimeout(function () {
            $('#calendar').fullCalendar('refetchEvents');
            $('#calendar').fullCalendar('rerenderEvents');
        },
            100);
    });

    calendar = $('#calendar').fullCalendar({
        themeSystem: 'bootstrap3',
        slotDuration: "00:15:00",
        defaultView: 'month',
        header: {
            left: 'prev,next,today',
            center: 'title',
            right: 'month,agendaWeek,listWeek,agendaDay'
        },
        displayEventTime: true,
        displayEventEnd: true,
        allDaySlot: false,
        height: "auto",
        validRange: {
            start: moment().format("YYYY-MM-DD")
        },
        selectable: true,
        selectHelper: true,
        nowIndicator: true,
        eventLimitText: 'events',
        eventBackgroundColor: '#3a87ad59',
        eventBorderColor: '#3a87ad59',
        eventTextColor: '#000000d6',
        eventLimit: 1,
        selectAllow: function (info) {
            if (calendar.fullCalendar('getView').name === 'month')
                return false;
            if (info.start.isBefore(moment()))
                return false;
            return true;
        },
        select: function (startDate, endDate) {
            $('#periodicity').val(6).trigger('change');
            $('#one-time-datetimepicker').data("DateTimePicker").date(startDate);
            var duration = moment.duration(endDate.diff(startDate)).as('minutes');
            $('#schedule-mode .duration').val(duration);
            $("a[href='#schedule-mode']").click();
        },
        events: function (start, end, timezone, callback) {
            var events = [];

            var processInterval = function (interval, slot) {
                var occurences = interval.all();
                for (var i = 0; i < occurences.length; i++) {
                    occurences[i].minute(slot.minute).hour(slot.hour).second(slot.second || 0);
                    var startDate = moment(occurences[i]);
                    if (startDate < moment()) {
                        continue;
                    }
                    var endDate = moment(startDate);
                    endDate.add(slot.duration, 'm');
                    events.push({
                        title: slot.title,
                        start: startDate,
                        end: endDate
                    });
                };
            };

            if (calendarEvents.slots !== undefined) {
                $.each(calendarEvents.slots, function (index, slot) {
                    if (slot.enabled) {

                        var currDate = moment(start);
                        if (start < moment()) {
                            currDate = moment();
                        }

                        switch (slot.periodicity) {
                            case 0:
                                currDate.minute(slot.minute).second(slot.second);
                                if (currDate < moment()) {
                                    currDate.add(1, 'h');
                                }
                                while (currDate >= start && currDate < end) {
                                    var endDate = moment(currDate);
                                    endDate.add(slot.duration, 'm');
                                    var startDate = moment(currDate);
                                    events.push({
                                        title: slot.title,
                                        start: startDate,
                                        end: endDate
                                    });

                                    currDate.add(1, 'h');
                                }
                                break;
                            case 1:
                                currDate.minute(slot.minute).second(slot.second);
                                if (currDate < moment()) {
                                    currDate.add(slot.hours, 'h');
                                }
                                while (currDate >= start && currDate < end) {
                                    var endDate = moment(currDate);
                                    endDate.add(slot.duration, 'm');
                                    var startDate = moment(currDate);
                                    events.push({
                                        title: slot.title,
                                        start: startDate,
                                        end: endDate
                                    });

                                    currDate.add(slot.hours, 'h');
                                }
                                break;
                            case 2:
                                var interval = moment(currDate).recur(start, end).every(1).day();
                                processInterval(interval, slot);
                                break;
                            case 3:
                                var interval = moment(currDate).recur(start, end).every(slot.days).day();
                                processInterval(interval, slot);
                                break;
                            case 4:
                                var interval = moment(currDate).recur(start, end).every(weekNames[slot.dayOfWeek]).dayOfWeek();
                                processInterval(interval, slot);
                                break;
                            case 5:
                                var interval = moment(currDate).recur(start, end).every(slot.dayOfMonth).dayOfMonth();
                                processInterval(interval, slot);
                                break;
                            case 6:
                                var startDate = moment([slot.year, slot.month - 1, slot.day, slot.hour, slot.minute, slot.second, 0]);
                                var endDate = moment(startDate);
                                endDate.add(slot.duration, 'm');
                                events.push({
                                    title: slot.title,
                                    start: startDate,
                                    end: endDate
                                });
                                break;
                        }
                    }
                });

                callback(events);
            }
        },
        viewRender: function (view, element) {
            fetchWeatherForecast();
        }
    })

    $('.time-minute, .time-second').pickatime({
        format: 'i',
        interval: 1,
        min: [0, 0],
        max: [0, 59],
        clear: '',
        onSet: getSchedule
    });

    $('.time-hour-minute').pickatime({
        format: 'HH:i',
        interval: 15,
        clear: '',
        onSet: getSchedule
    }).pickatime('picker').set('select', 0);

    $('.time-hours').pickatime({
        format: 'HH',
        interval: 60,
        min: [2, 0],
        max: [23, 0],
        clear: '',
        onSet: getSchedule
    }).pickatime('picker').set('select', 2);

    $('.time-day-of-month').pickatime({
        format: 'i',
        interval: 1,
        min: [0, 1],
        max: [0, 28], //Minimum days for each months
        clear: '',
        onSet: getSchedule
    }).pickatime('picker').set('select', 1);

    $('.time-minute, .time-second, .time-hour-minute, .time-hours, .time-day-of-month').each(function () {
        $(this).pickatime('picker').set('select', 0);
    });

    $('a[data-toggle="tab"]').click(function () {
        if ($('.navbar-collapse.collapse.in').length) {
            $('button.navbar-toggle').click();
        }
    });

    $('#periodicity').change(function () {
        switch (parseInt($(this).val())) {
            case 0:
                $('.period-block ').hide();
                $('.period-block.hourly').show();
                break;
            case 1:
                $('.period-block ').hide();
                $('.period-block.every-x-hours').show();
                break;
            case 2:
                $('.period-block ').hide();
                $('.period-block.daily').show();
                break;
            case 3:
                $('.period-block ').hide();
                $('.period-block.every-x-days').show();
                break;
            case 4:
                $('.period-block ').hide();
                $('.period-block.weekly').show();
                break;
            case 5:
                $('.period-block ').hide();
                $('.period-block.monthly').show();
                break;
            case 6:
                $('.period-block ').hide();
                $('.period-block.one-time').show();
                break;
            default:
                $('.period-block ').hide();
                break;
        }

        getSchedule();
    });

    $('#periodicity').change();

    $('#schedule-mode-zones').change(getSchedule);
    $('.time-days').change(getSchedule);
    $('#weekdays-selector input').change(getSchedule);
    $('#schedule-mode .duration').change(getSchedule);

    $(document).on('click', '.event-actions .action-remove', function () {
        var evId = parseInt($(this).closest('tr').find('td:first').text());
        removeEvent(evId);
    });

    $(document).on('click', '.event-actions .action-edit', function () {
        var evId = parseInt($(this).closest('tr').find('td:first').text());
        var slot = getSlotById(evId);
        if (null == slot) {
            return;
        }

        cancelEditSchedule();

        $('#evId').val(evId);
        $('html, body').animate({
            scrollTop: $("#add-event-header").offset().top
        }, 500);
        $('#events-list tbody td.evId[data-evid="' + evId + '"]').closest('tr').addClass('bg-warning');
        $('#add-event-header').text('Edit event #' + evId);
        $('#schedule-mode .add-schedule').hide();
        $('#schedule-mode .save-schedule').show();
        $('#schedule-mode .cancel-edit-schedule').show();
        $('#schedule-mode .event-title').val(slot.title);
        $('#schedule-mode-zones option:selected').removeAttr('selected');
        $.each(slot.channels, function (index, value) {
            $(`#schedule-mode-zones option:eq(${value})`).attr('selected', true);
        });

        $('#periodicity').val(slot.periodicity).trigger('change');
        $('#schedule-mode .duration').val(slot.duration).trigger('change');

        switch (slot.periodicity) {
            case 0:
                var $periodBlock = $('.period-block.hourly');
                $periodBlock.find('.time-minute').pickatime().pickatime('picker').set('select', slot.minute);
                $periodBlock.find('.time-second').pickatime().pickatime('picker').set('select', slot.second);
                break;
            case 1:
                var $periodBlock = $('.period-block.every-x-hours');
                $periodBlock.find('.time-hours').pickatime().pickatime('picker').set('select', [slot.hours, 0]);
                $periodBlock.find('.time-minute').pickatime().pickatime('picker').set('select', slot.minute);
                $periodBlock.find('.time-second').pickatime().pickatime('picker').set('select', slot.second);
                break;
            case 2:
                var $periodBlock = $('.period-block.daily');
                $periodBlock.find('.time-hour-minute').pickatime().pickatime('picker').set('select', [slot.hour, slot.minute]);
                break;
            case 3:
                var $periodBlock = $('.period-block.every-x-days');
                $periodBlock.find('.time-days').val(slot.days).trigger('change');
                $periodBlock.find('.time-hour-minute').pickatime().pickatime('picker').set('select', [slot.hour, slot.minute]);
                break;
            case 4:
                var $periodBlock = $('.period-block.weekly');
                $periodBlock.find('.time-hour-minute').pickatime().pickatime('picker').set('select', [slot.hour, slot.minute]);

                $('#weekdays-selector label').removeClass('active');
                $('#weekdays-selector input[value="' + slot.dayOfWeek + '"]').parent().addClass('active');
                break;
            case 5:
                var $periodBlock = $('.period-block.monthly');
                $periodBlock.find('.time-hour-minute').pickatime().pickatime('picker').set('select', [slot.hour, slot.minute]);
                $periodBlock.find('.time-day-of-month').pickatime().pickatime('picker').set('select', slot.dayOfMonth);
                break;
            case 6:

                var date = moment([slot.year, slot.month - 1, slot.day, slot.hour, slot.minute, slot.second, 0]);
                console.log(date);
                $('#one-time-datetimepicker').data("DateTimePicker").date(date);
                break;
        }
    });

    $(document).on('click', '.event-actions .action-disable', function () {
        var evId = parseInt($(this).closest('tr').find('td:first').text());
        setEventEnabled(evId, false);
    });

    $(document).on('click', '.event-actions .action-enable', function () {
        var evId = parseInt($(this).closest('tr').find('td:first').text());
        setEventEnabled(evId, true);
    });

    $(document).on('click', '.cancel-edit-schedule', cancelEditSchedule);

    $(document).on('click', '.running-info .skip-btn', function () {
        var evId = parseInt($(this).closest('.running-info').attr('data-evid'));
        if (evId === 25) {
            if (confirm("Are you sure?")) {
                stopManualIrrigation();
            }
        } else {
            skipEvent(evId);
        }
    });

    $(document).on('click', 'a[href="#schedule-mode"]', function () { getSchedule(); });

    $(document).on('click', '.save-channel-names-btn', function () {
        $(this).prop('disabled', true);
        var channelNames = [];
        $('#channel-names tbody tr').each(function () {
            var id = parseInt($(this).find('td:eq(0)').html());
            var name = $(this).find('td:eq(1) input').val();
            if (name.trim().length <= 0) {
                alert(`Wrong name "${name}" for channel ${id}`);
                $(this).find('tr:eq(1) input').focus();
            } else {
                channelNames.push({ 'id': id, 'name': name });
            }
        });
        var command = {};
        command.command = "saveChannelNames";
        command.data = channelNames;
        ws.send(JSON.stringify(command));
    });

    $('.location-typeahead').typeahead({
        delay: 1000,
        source: function (query, process) {
            return $.get('http://api.apixu.com/v1/search.json?key=4e3720d2b7234ec8b8585710191907', { q: query }, function (data) {
                var cities = [];
                $.each(data, function (index, element) {
                    cities.push(element.name);
                });
                return process(cities);
            });
        }
    });

    $(document).on('click', '.save-location-btn', function () {
        var command = {};
        command.command = "saveSettings";
        command.data = {};
        command.data.location = $('.location-typeahead').val();
        console.log(command);
        ws.send(JSON.stringify(command));
    });
});

function getMomentFromEpoch(epoch) {
    return moment(moment.unix(epoch).utc().format("YYYY-MM-DD HH:mm:ss"));
}

function cancelEditSchedule() {
    $('#evId').val('');
    $('#events-list tbody tr').removeClass('bg-warning');
    $('#add-event-header').text('Add event');
    $('#schedule-mode .add-schedule').show();
    $('#schedule-mode .save-schedule').hide();
    $('#schedule-mode .cancel-edit-schedule').hide();
    $('#schedule-mode-zones option:selected').removeAttr('selected');
    $('#schedule-mode-zones option:first').attr('selected', true);
    $('#periodicity').val(0).trigger('change');
    $('#schedule-mode .duration').val(5).trigger('change');
    var $periodBlock = $('.period-block.hourly');
    $periodBlock.find('.time-minute').pickatime().pickatime('picker').set('select', 0);
    $periodBlock.find('.time-second').pickatime().pickatime('picker').set('select', 0);
}

function notify(text, type) {
    $.notify(text, { type: type, placement: { from: 'top', align: 'right', delay: 2000 } });
}

function getSlotById(evId) {
    return calendarEvents.slots[evId] || null;
};

function processSlots(data = null) {
    calendarEvents = data;
    var $eventTableBody = $('#events-list tbody');
    if (null !== calendarEvents) {
        $('#calendar').fullCalendar('refetchEvents');
        $('#calendar').fullCalendar('rerenderEvents');

        var slots = calendarEvents.slots;
        $eventTableBody.empty();
        var total = parseInt(calendarEvents.total);
        var occupied = parseInt(calendarEvents.occupied);
        var available = total - occupied;
        var statisticText = `<b>${occupied}</b> slots out of <b>${total}</b> are occupied. <b>${available}</b> slots available for adding`;
        $('#events-statistic').html(statisticText);

        availableSlots = available;

        $('#schedule-mode .add-schedule').prop('disabled', availableSlots <= 0);

        for (var i = 0; i < total; i++) {
            var slot = getSlotById(i);
            var enabled = null !== slot ? slot.enabled : true;
            enabled = isNaN(enabled) ? true : enabled;
            var tr = '<tr><td colspan="5">Free slot</td></tr>';
            var disableBtn = `<li role="presentation"><a class="action-disable" role="menuitem"><i class="fa fa-power-off"></i> Disable</a></li>`;
            var enableBtn = `<li role="presentation"><a class="action-enable" role="menuitem"><i class="fa fa-play text-success"></i> Enable</a></li>`;
            var enableDisableBtn = enabled ? disableBtn : enableBtn;

            var actions = `<td class="event-actions">
                <div class="dropdown">
                <button class="btn btn-xs btn-default dropdown-toggle action-btn" type="button" data-toggle="dropdown">Actions <span class="caret"></span></button>
                <ul class="dropdown-menu dropdown-menu-right" role="menu">
                <li role="presentation">
                <a class="action-edit" role="menuitem"><i class="fa fa-edit text-warning"></i> Edit</a>
                </li>
                <li role="presentation">
                <a class="action-remove" role="menuitem"><i class="fa fa-trash text-danger"></i> Remove</a>
                </li>
                ${enableDisableBtn}
                </ul>
                </div>
                </td>`;

            if (null !== slot) {
                var duration = moment.duration(slot.duration, 'minutes').format('HH[h]:mm[m]');
                var periodicity = periodicityList[slot.periodicity] || null;
                var channels = slot.channels ? JSON.stringify(slot.channels) : '';
                var color = enabled ? "#333" : "#999";

                tr = `<tr data-enabled="${enabled}" style="color: ${color}">
                    <td class="evId" data-evid="${i}">${i}</td>
                    <td>${periodicity}</td>
                    <td>${duration}</td>
                    <td>${channels}</td>
                    ${actions}
                    </tr>`;
            }

            $eventTableBody.append(tr);
        }
    }
}

function WebSocketBegin(location) {
    if ("WebSocket" in window) {
        ws = new WebSocket(location);
        ws.onopen = function () {
            $('#ws-n-conn').hide();
            getSettings();
            getSysInfo();
            getChannelNames();
            getSlots();
        };

        ws.onmessage = function (evt) {
            var jsonObject = JSON.parse(evt.data);

            console.log(jsonObject);
            var command = jsonObject.command || null;
            if (null !== command) {
                var data = jsonObject.data || null;
                var msg = jsonObject.msg || null;
                switch (command) {
                    case 'stopManualIrrigation':
                        $('#manual-mode .stop-irrigation-btn').hide();
                        $('#manual-mode .start-irrigation-btn').show();
                        notify('Manual irrigation finished', 'success');
                        break;
                    case 'addOrEditSchedule':
                        notify('Schedule added/updated', 'success');
                        cancelEditSchedule();
                        break;
                    case 'getSlots':
                        processSlots(data);
                        $('.add-schedule').prop("disabled", !(availableSlots > 0));
                        getSchedule();
                        break;
                    case 'getSysInfo':
                        if (data['WiFi']) {
                            $('#wifi-ssid').text(data['WiFi']['SSID']);
                            $('#wifi-ip').text(data['WiFi']['localIP']);
                            $('#wifi-rssi').text(data['WiFi']['RSSI']);
                        }

                        if (data['SD']) {
                            $('#sd-total').text(data['SD']['total']);
                            $('#sd-used').text(data['SD']['used']);
                            $('#sd-type').text(data['SD']['type']);
                        }

                        if (data['SPIFFS']) {
                            $('#spiffs-total').text(data['SPIFFS']['total']);
                            $('#spiffs-used').text(data['SPIFFS']['used']);
                        }

                        if (data['heap']) {
                            var total = Math.floor(parseInt(data['heap']['total']) / 1024);
                            var free = Math.floor(parseInt(data['heap']['free']) / 1024);
                            var min = Math.floor(parseInt(data['heap']['min']) / 1024);
                            var max = Math.floor(parseInt(data['heap']['maxAlloc']) / 1024);

                            $('#mem-total').text(total);
                            $('#mem-free').text(free);
                            $('#mem-min').text(min);
                            $('#mem-max').text(max);
                        }

                        if (data['gsm']) {
                            $('#gsm-balance').text(data['gsm']['balance'] + "₴");
                            var status = "N/A";
                            switch (data['gsm']['CREGCode']) {
                                case 0:
                                    status = "Not registered";
                                    break;
                                case 1:
                                    status = "Registered";
                                    break;
                                case 2:
                                    status = "Search";
                                    break;
                                case 3:
                                    status = "Declined";
                                    break;
                                case 4:
                                    status = "Unknown";
                                    break;
                                case 5:
                                    status = "Roaming";
                                    break;
                            }
                            $('#gsm-status').text(status);
                            $('#gsm-signal').text(data['gsm']['signal']);
                            $('#gsm-phone').text(data['gsm']['phone']);
                        }

                        currentTime = moment(data['time']);
                        $('#current-time').text(currentTime.format('ddd, YYYY-MM-DD HH:mm:ss'));
                        break;
                    case 'ongoingEvents':
                        $('.zone-panel').removeClass('active');
                        $('.start-date, .finish-date, .elapsed-time, .running-info .event-name').html("N/A");
                        data.sort(compareOccurences);
                        $('#manual-mode .stop-irrigation-btn').hide();
                        $('#manual-mode .start-irrigation-btn').show();
                        $.each(data, function (index, occurence) {
                            if (occurence.isManual) {
                                $('#manual-mode .stop-irrigation-btn').show();
                                $('#manual-mode .start-irrigation-btn').hide();
                                $.each(occurence.channels, function (index, value) {
                                    $(`#manual-mode-zones option:eq(${index})`).attr('selected', value == 1);
                                });
                            }
                            $.each(occurence.channels, function (index, zone) {
                                var $zonePanelBody = $('div[data-zone="' + index + '"]');
                                var $zonePanel = $zonePanelBody.closest('.zone-panel');
                                if (zone && !$zonePanel.hasClass('active')) {
                                    var startDate = getMomentFromEpoch(occurence.from);
                                    var finishDate = getMomentFromEpoch(occurence.to);
                                    var elapsed = moment.duration(occurence.elapsed, "seconds").format("D[d] H[h] m[m] s[s]");
                                    $zonePanel.find('.start-date').html(startDate.format('YYYY-MM-DD HH:mm:ss'));
                                    $zonePanel.find('.finish-date').html(finishDate.format('YYYY-MM-DD HH:mm:ss'));
                                    $zonePanel.find('.duration').html(moment.duration((finishDate - startDate), "milliseconds").format("D[d] H[h] m[m] s[s]"));
                                    $zonePanel.find('.elapsed-time').html(elapsed);
                                    if (Object.entries(calendarEvents).length > 0) {
                                        $zonePanel.find('.running-info .event-name').html(occurence.evId === 25 ? "Manual" : calendarEvents.slots[occurence.evId].title);
                                    }
                                    $zonePanel.addClass('active');
                                    $zonePanelBody.find('.running-info').attr("data-evid", occurence.evId);
                                }
                            });
                        });
                        break;
                    case 'nextEvents':
                        $('.next-start-date, .next-finish-date, .next-elapsed, .next-duration, .next-start .event-name').html("N/A");
                        data.sort(compareOccurences);
                        $.each(data, function (index, occurence) {
                            $.each(occurence.channels, function (index, zone) {
                                var $zonePanelBody = $('div[data-zone="' + index + '"]:not(.active)');
                                var $zonePanel = $zonePanelBody.closest('.zone-panel');
                                if (zone) {
                                    var startDate = getMomentFromEpoch(occurence.from);
                                    var finishDate = getMomentFromEpoch(occurence.to);
                                    var elapsed = moment.duration(occurence.elapsed, "seconds").format("D[d] H[h] m[m] s[s]");
                                    $zonePanel.find('.next-start-date').html(startDate.format('YYYY-MM-DD HH:mm:ss'));
                                    $zonePanel.find('.next-finish-date').html(finishDate.format('YYYY-MM-DD HH:mm:ss'));
                                    $zonePanel.find('.next-duration').html(moment.duration((finishDate - startDate), "milliseconds").format("D[d] H[h] m[m] s[s]"));
                                    $zonePanel.find('.next-elapsed').html(elapsed);
                                    if (Object.entries(calendarEvents).length) {
                                        $zonePanel.find('.next-start .event-name').html(occurence.evId === 25 ? "Manual" : calendarEvents.slots[occurence.evId].title);
                                    }
                                }
                            });
                        });
                        break;
                    case 'getWaterInfo':
                        $('#water-flow').text(data['flow']);
                        $('#water-day').text(data['curDay']);
                        $('#water-month').text(data['curMonth']);
                        break;
                    case 'weatherUpdate':
                        $('.w-temp').html(`${data.temp}°C`);
                        $('.w-press').html(`${data.pressure} hPa`);
                        $('.w-hum').html(`${data.humidity}%`);
                        $('.w-light').html(`${data.light} lux`);
                        $('.w-water-temp').html(`${data.waterTemp}°C`);
                        $('.w-rain').html(`${data.rain}`);
                        $('.w-ground-hum').html(`${data.groundHum}`);
                        break;
                    case 'getChannelNames':
                        var isEdited = $('.save-channel-names-btn').is(":disabled");

                        if (isEdited) {
                            notify('Channel names has been saved.', 'success');
                            $('.save-channel-names-btn').prop('disabled', false);
                            $('#channel-statuses').empty();
                            $('#manual-mode-zones, #schedule-mode-zones').empty();
                            $('#channel-names tbody').empty();
                        }

                        if ($('#channel-statuses .channel-id').length <= 0) {
                            var channelsTemplate = $("#dash-channel-status-block div:first").clone();
                            var channelBlock = $("#channel-statuses");
                            $.each(data, function () {
                                var channelStatus = channelsTemplate.clone();
                                channelStatus.find('.channel-id').attr('data-zone', this.id);
                                channelStatus.find('.channel-name').html(this.name);
                                channelStatus.show();
                                channelBlock.append(channelStatus);
                                $('#manual-mode-zones, #schedule-mode-zones').append(`<option value="${this.id}">${this.name}</option>`);
                                var channelNamesBody = $('#channel-names tbody');
                                channelNamesBody.append(
                                    `<tr>
                                        <td style="vertical-align: middle;">${this.id}</td>
                                        <td style="padding: 0;">
                                            <input style="border: none; padding: 0px 5px;" class="form-control input-sm" maxlength="25" value="${this.name}"></input>
                                        </td>
                                    </tr>`
                                );
                            });
                        }
                        break;
                    case 'debug':
                        $('#logs-textarea').append(`${msg}\r\n`);
                        break;
                    case 'getSettings':
                        settings.location = data["location"];

                        $('.location-typeahead').val(settings.location);
                        break;
                }
            }
        };

        ws.onclose = function () {
            $('#ws-n-conn').show();
            setTimeout(function () { WebSocketBegin(websocketServerLocation) }, 5000);
        };

        ws.onerror = function (error) {
            if (error.message !== undefined) {
                notify('WS error: ' + error.message, 'danger');
            }
        };
    } else {
        notify('WebSocket NOT supported by your Browser!', 'danger');
    }
}

function compareOccurences(a, b) {
    return a.from < b.from ? 1 : -1;
}

function manualIrrigation() {
    if ($('#manual-mode-zones option:selected').length === 0) {
        alert("Select minimum one zone!");
        $('#manual-mode-zones option:first').attr('selected', true);
        return;
    }
    var command = {};
    command.command = "manualIrrigation";
    command.data = {};
    var checked = []
    $('#manual-mode-zones option:selected').each(function () {
        checked.push(parseInt($(this).val()));
    });
    command.data.duration = parseInt($('#duration').val());
    command.data.channels = checked;

    ws.send(JSON.stringify(command));
}

function getSchedule() {
    var $scheduleBlock = $('#schedule-mode');
    var eventSlot = {};
    var checked = [];
    var evId = parseInt($('#evId').val());

    $('#schedule-mode-zones option:selected').each(function () {
        checked.push(parseInt($(this).val()));
    });

    eventSlot.duration = parseInt($scheduleBlock.find('.duration').val());
    eventSlot.channels = checked;
    eventSlot.title = $('.event-title').val();
    // If edit event
    if (!isNaN(evId)) {
        eventSlot.evId = evId;
    }

    switch (parseInt($('#periodicity').val())) {
        case 0:
            var $periodBlock = $('.period-block.hourly');
            eventSlot.minute = parseInt($periodBlock.find('.time-minute').val());
            eventSlot.second = parseInt($periodBlock.find('.time-second').val());
            eventSlot.periodicity = 0;

            if (eventSlot.duration <= 0 || eventSlot.duration >= 59) {
                $scheduleBlock.find('.duration').val(59);
                eventSlot.duration = 59;
            }
            break;
        case 1:
            var $periodBlock = $('.period-block.every-x-hours');
            eventSlot.hours = parseInt($periodBlock.find('.time-hours').val());
            eventSlot.minute = parseInt($periodBlock.find('.time-minute').val());
            eventSlot.second = parseInt($periodBlock.find('.time-second').val());
            eventSlot.periodicity = 1;

            var maxDuration = (eventSlot.hours * 60) - 1;
            if (eventSlot.duration <= 0 || eventSlot.duration >= maxDuration) {
                $scheduleBlock.find('.duration').val(maxDuration);
                eventSlot.duration = maxDuration;
            }
            break;
        case 2:
            var time = $('.period-block.daily').find('.time-hour-minute').val();
            var timeArray = time.split(":");
            eventSlot.hour = parseInt(timeArray[0]);
            eventSlot.minute = parseInt(timeArray[1]);
            eventSlot.periodicity = 2;

            var maxDuration = 23 * 60;
            if (eventSlot.duration <= 0 || eventSlot.duration >= maxDuration) {
                $scheduleBlock.find('.duration').val(maxDuration);
                eventSlot.duration = maxDuration;
            }
            break;
        case 3:
            var $periodBlock = $('.period-block.every-x-days');
            var time = $periodBlock.find('.time-hour-minute').val();
            var timeArray = time.split(":");
            eventSlot.days = parseInt($periodBlock.find('.time-days').val());
            eventSlot.hour = parseInt(timeArray[0]);
            eventSlot.minute = parseInt(timeArray[1]);
            eventSlot.periodicity = 3;

            var maxDuration = ((eventSlot.days * 24) - 1) * 60;
            if (eventSlot.duration <= 0 || eventSlot.duration >= maxDuration) {
                $scheduleBlock.find('.duration').val(maxDuration);
                eventSlot.duration = maxDuration;
            }
            break;
        case 4:
            var $periodBlock = $('.period-block.weekly');
            var time = $periodBlock.find('.time-hour-minute').val();
            var timeArray = time.split(":");
            eventSlot.dayOfWeek = parseInt($('#weekdays-selector label.active').find('input').val());
            eventSlot.hour = parseInt(timeArray[0]);
            eventSlot.minute = parseInt(timeArray[1]);
            eventSlot.periodicity = 4;

            var maxDuration = ((7 * 24) - 1) * 60;
            if (eventSlot.duration <= 0 || eventSlot.duration >= maxDuration) {
                $scheduleBlock.find('.duration').val(maxDuration);
                eventSlot.duration = maxDuration;
            }
            break;
        case 5:
            var $periodBlock = $('.period-block.monthly');
            var time = $periodBlock.find('.time-hour-minute').val();
            var timeArray = time.split(":");
            eventSlot.dayOfMonth = parseInt($periodBlock.find('.time-day-of-month').val());
            eventSlot.hour = parseInt(timeArray[0]);
            eventSlot.minute = parseInt(timeArray[1]);
            eventSlot.periodicity = 5;

            var maxDuration = ((30 * 24) - 1) * 60;
            if (eventSlot.duration <= 0 || eventSlot.duration >= maxDuration) {
                $scheduleBlock.find('.duration').val(maxDuration);
                eventSlot.duration = maxDuration;
            }
        case 6:
            var date = $('#one-time-datetimepicker').data("DateTimePicker").viewDate();
            eventSlot.year = date.year();
            eventSlot.month = parseInt(date.format('MM'));
            eventSlot.day = date.date();
            eventSlot.hour = date.hour();
            eventSlot.minute = date.minute();
            eventSlot.second = date.second();
            eventSlot.periodicity = 6;
            break;
    }

    $("#explanation").html(getExplanationForSchedule(eventSlot));

    return eventSlot;
}

function getExplanationForSchedule(scheduleObject) {
    let zonesString = scheduleObject.channels.join(', ');
    let durationStr = moment.duration(scheduleObject.duration, 'minutes').format('HH[h]:mm[m]');
    let title = "";
    var on = "";
    var getExampleText = function (currDate, scheduleObject) {
        var endDate = moment(currDate);
        var format = 'YYYY-MM-DD HH:mm:ss';
        endDate.add(scheduleObject.duration, 'm');
        return `<p>Ex: ${currDate.format(format)} - ${endDate.format(format)}</p>`;
    }

    switch (scheduleObject.periodicity) {
        case 0:
            var currDate = moment();
            currDate.minute(scheduleObject.minute).second(scheduleObject.second);
            on = `${currDate.format('mm:ss')}`;
            explanationString = `Irrigation for zone(s) ${zonesString} every hour on ${currDate.format('mm[m]:ss[s]')} with a duration of ${durationStr}\n`;
            title = "Every hour";

            for (var i = 0; i < 3; i++) {
                currDate.add(1, 'h');
                explanationString += getExampleText(currDate, scheduleObject);
            }
            break;
        case 1:
            var currDate = moment();
            currDate.minute(scheduleObject.minute).second(scheduleObject.second);
            on = `${currDate.format('mm:ss')}`;
            explanationString = `Irrigation for zone(s) ${zonesString} every ${scheduleObject.hours} hours on ${currDate.format('mm[m]:ss[s]')} with a duration of ${durationStr}\n`;
            title = `Every ${scheduleObject.hours} hours`;

            for (var i = 0; i < 3; i++) {
                currDate.add(scheduleObject.hours, 'h');
                explanationString += getExampleText(currDate, scheduleObject);
            }
            break;
        case 2:
            var currDate = moment();
            currDate.hour(scheduleObject.hour).minute(scheduleObject.minute).second(0);
            on = `${currDate.format('HH:mm')}`;
            explanationString = `Irrigation for zone(s) ${zonesString} every day on ${currDate.format('HH[h]:mm[m][00s]')} with a duration of ${durationStr}\n`;
            title = `Every day`;

            for (var i = 0; i < 3; i++) {
                currDate.add(1, 'd');
                explanationString += getExampleText(currDate, scheduleObject);
            }
            break;
        case 3:
            var currDate = moment();
            currDate.hour(scheduleObject.hour).minute(scheduleObject.minute).second(0);
            on = `${currDate.format('HH:mm')}`;
            explanationString = `Irrigation for zone(s) ${zonesString} every ${scheduleObject.days} days on ${currDate.format('HH[h]:mm[m][00s]')} with a duration of ${durationStr}\n`;
            title = `Every ${scheduleObject.days} days`;

            for (var i = 0; i < 3; i++) {
                currDate.add(scheduleObject.days, 'd');
                explanationString += getExampleText(currDate, scheduleObject);
            }
            break;
        case 4:
            var currDate = moment();
            var dayOfWeekStr = weekNames[scheduleObject.dayOfWeek - 1];
            currDate.day(scheduleObject.dayOfWeek - 1).hour(scheduleObject.hour).minute(scheduleObject.minute).second(0);
            on = `${currDate.format('HH:mm')}`;
            explanationString = `Irrigation for zone(s) ${zonesString} every ${dayOfWeekStr} on ${currDate.format('HH[h]:mm[m][00s]')} with a duration of ${durationStr}\n`;
            title = `Every ${dayOfWeekStr}`;

            for (var i = 0; i < 3; i++) {
                currDate.add(1, 'w');
                explanationString += getExampleText(currDate, scheduleObject);
            }
            break;
        case 5:
            var currDate = moment();
            currDate.hour(scheduleObject.hour).minute(scheduleObject.minute).second(0);
            currDate.date(scheduleObject.dayOfMonth);
            on = `${currDate.format('HH:mm')}`;
            explanationString = `Irrigation for zone(s) ${zonesString} every month on ${currDate.format('HH[h]:mm[m][00s]')} with a duration of ${durationStr}\n`;
            title = `Every month`;

            for (var i = 0; i < 3; i++) {
                currDate.add(1, 'M');
                explanationString += getExampleText(currDate, scheduleObject);
            }
        case 6:
            var currDate = moment([scheduleObject.year, scheduleObject.month - 1, scheduleObject.day, scheduleObject.hour, scheduleObject.minute, scheduleObject.second, 0]);
            on = `${currDate.format('YYYY-MM-DD HH:mm:ss')}`;
            explanationString = `Irrigation for zone(s) ${zonesString} on ${currDate.format('YYYY-MM-DD HH:mm:ss')} with a duration of ${durationStr}\n`;
            title = `Once`;
            break;
    }

    var total = parseInt(calendarEvents.occupied) || 0;
    title = `${total}. ${title} on ${on}`;

    $('.event-title').val(title);

    return explanationString;
}

function addOrEditSchedule() {
    var eventSlot = getSchedule();

    if (availableSlots <= 0 && isNaN(eventSlot.evId)) {
        alert("There are no available slots");
        return;
    }

    if ($('#schedule-mode-zones option:selected').length === 0) {
        alert("Select minimum one zone!");
        $('#schedule-mode-zones option:selected').attr('selected', true);
        return;
    }

    if (confirm("Are you sure?")) {
        var command = {};
        command.command = "addOrEditSchedule";
        command.data = eventSlot;
        console.log(command, JSON.stringify(command));
        $('.add-schedule').prop("disabled", 1);
        ws.send(JSON.stringify(command));
    }
}

function stopManualIrrigation() {
    ws.send('{"command": "stopManualIrrigation"}');
    $('#manual-mode .stop-irrigation-btn').hide();
    $('#manual-mode .start-irrigation-btn').show();
}

function saveWifiConfig() {
    var command = {};
    command.command = "WiFiConfig";
    command.data = {};
    command.data.ssid = $("#ssid").value;
    command.data.pass = $("#pass").value;

    ws.send(JSON.stringify(command));
}

function getSlots() {
    var command = {};
    command.command = "getSlots";

    ws.send(JSON.stringify(command));
}

function removeEvent(evId) {
    var command = {};
    command.command = "removeEvent";
    command.data = {};
    command.data.evId = evId;

    ws.send(JSON.stringify(command));
    $('.action-btn').prop('disabled', 1);
}

function setEventEnabled(evId, enabled = true) {
    var command = {};
    command.command = "setEventEnabled";
    command.data = {};
    command.data.evId = evId;
    command.data.enabled = enabled;

    ws.send(JSON.stringify(command));
    $('.action-btn').prop('disabled', 1);
}

function getSysInfo() {
    var command = {};
    command.command = "getSysInfo";

    ws.send(JSON.stringify(command));
}

function getWaterInfo() {
    var command = {};
    command.command = "getWaterInfo";

    ws.send(JSON.stringify(command));
}

function skipEvent(evId) {
    if (confirm("Are you sure?")) {
        var command = {};
        command.command = "skipEvent";
        command.data = {};
        command.data.evId = evId;

        ws.send(JSON.stringify(command));
    }
}

function getChannelNames() {
    var command = {};
    command.command = "getChannelNames";
    ws.send(JSON.stringify(command));
}

function setTime(date) {
    var command = {};
    command.command = "setTime";
    command.data = {};
    command.data.year = date.year();
    command.data.month = parseInt(date.format('MM'));
    command.data.day = date.date();
    command.data.hour = date.hour();
    command.data.minute = date.minute();
    command.data.second = date.second();
    ws.send(JSON.stringify(command));
    currentTime = date;
    console.log(command);
}

function fetchWeatherForecast() {
    var process = function (weatherData) {
        $('.weather-block').remove();
        $.each(weatherData.forecast.forecastday, function (index, forecast) {
            console.log(forecast);
            var monthDayElement = $(`td.fc-day[data-date="${forecast.date}"]`);
            var weekHeaderElement = $(`th.fc-day-header[data-date="${forecast.date}"]`);
            var dayIcon = forecast.day.condition.icon;
            var dayIconAlt = forecast.day.condition.text;
            var tempMin = forecast.day.mintemp_c;
            var tempMax = forecast.day.maxtemp_c;
            var totalprecip = forecast.day.totalprecip_mm;
            var uvIndex = forecast.day.uv;
            var uvIndexColor = "green";
            var uvIndexTitle = "No protection required";
            if (uvIndex >= 3 && uvIndex <= 5) {
                uvIndexColor = "#f8af44";
                uvIndexTitle = "Protection required";
            } else if (uvIndex > 5 && uvIndex <= 7) {
                uvIndexColor = "#f7941e";
                uvIndexTitle = "Protection essential";
            } else if (uvIndex > 7 && uvIndex <= 10) {
                uvIndexColor = "#de401f";
                uvIndexTitle = "Need shade";
            } else if (uvIndex > 11) {
                uvIndexColor = "#aa5b99";
                uvIndexTitle = "Can't go outdor";
            }

            var template = `
            <div class='weather-block'>
                <img src='${dayIcon}' alt='${dayIconAlt}' title='${dayIconAlt}'/>
                <div style="margin-left: 6px" class="hidden-xs">
                    <span title="Min temperature" style="font-size: 12px"><i class="fa fa-temperature-low"/> ${tempMin}°C</span>                    
                    <span title="Max temperature" style="font-size: 12px"><i class="fa fa-temperature-high"/> ${tempMax}°C</span>
                </div>
                <div style="margin-left: 5px" class="hidden-xs">
                    <span title="UV-index(${uvIndexTitle})" style="font-size: 12px; color: ${uvIndexColor}"><i class="fa fa-sun"/> ${uvIndex}</span>    
                    <span title="Amount of precipitation" style="font-size: 12px"><i class="fa fa-tint"/> ${totalprecip}mm </span>                    
                </div>
            </div>`;

            monthDayElement.append(template);
            weekHeaderElement.append(template);
        });
    };
    var currentWeather = JSON.parse(localStorage.getItem("currentWeather"));
    var lastWeatherUpdate = currentWeather ? currentWeather.location.localtime_epoch : moment().unix();
    var needUpdate = moment().unix() - lastWeatherUpdate > 10800; // Last update more then 3h ago
    console.log(currentWeather, lastWeatherUpdate, moment().unix(), needUpdate);
    if (null === currentWeather || needUpdate) {
        $.get(`http://api.apixu.com/v1/forecast.json?key=${weatherAPIKey}&q=${settings.location}&days=10&lang=en`, function (weatherData) {
            console.log(weatherData);
            localStorage.setItem("currentWeather", JSON.stringify(weatherData));
            process(weatherData);
        });
    } else {
        process(currentWeather);
    }
}

function getSettings() {
    var command = {};
    command.command = "getSettings";
    ws.send(JSON.stringify(command));
}