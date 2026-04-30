$(document).ready(function() {
    var options = [];
    $('.panel-title a').on('click', function() { var checkboxControl = $(this).find("input[type='checkbox']"); if (checkboxControl.prop('checked') == false) { checkboxControl.prop('checked', true); } else { checkboxControl.prop('checked', false); } });
   
       $('body').on('change', '.sevice-access', function(event) {
        if ($(this).find('option:selected').text() == "MQTT") {
            $(this).parents('#service').addClass('mqtt-box-active');
            $(this).parents('#service').removeClass('http-box-active');
            $(this).parents('#service').removeClass('tcp-box-active');
            $(this).parents('#service').removeClass('socket-box-active');
        }
    });

    $('body').on('change', '.sevice-access', function(event) {
        if ($(this).find('option:selected').text() == "HTTP") {
            $(this).parents('#service').addClass('http-box-active');
            $(this).parents('#service').removeClass('mqtt-box-active');
            $(this).parents('#service').removeClass('tcp-box-active');
            $(this).parents('#service').removeClass('socket-box-active');
        }
    });

    $('body').on('change', '.sevice-access', function(event) {
        if ($(this).find('option:selected').text() == "TCP") {
            $(this).parents('#service').addClass('tcp-box-active');
            $(this).parents('#service').removeClass('mqtt-box-active');
            $(this).parents('#service').removeClass('http-box-active');
            $(this).parents('#service').removeClass('socket-box-active');
        }
    });

    $('body').on('change', '.sevice-access', function(event) {
        if ($(this).find('option:selected').text() == "Socket.io") {
            $(this).parents('#service').addClass('socket-box-active');
            $(this).parents('#service').removeClass('mqtt-box-active');
            $(this).parents('#service').removeClass('http-box-active');
            $(this).parents('#service').removeClass('tcp-box-active');
        }
    });



    

    $(document).on('change', '.mdb-select', function(event) {
        if ($(this).find('option:selected').text() == "static") {
            $(this).parents('#network').addClass('static-box-active');
        } else {
            $(this).parents('#network').removeClass('static-box-active');
        }
    });
    $(document).on('change', '.mdb-select-repeater', function(event) {
        if ($(this).find('option:selected').text() == "static") {
            $(this).parents('#network').addClass('inner-static-box-active');
        } else {
            $(this).parents('#network').removeClass('inner-static-box-active');
        }
    });

    $('body').on('click', '.dhcp-box', function(event) { $(this).parents('#network').removeClass('static-box-active'); });
    $('body').on('click', '.inner-static-box', function(event) { $(this).parents('#network').addClass('inner-static-box-active'); });
    $('body').on('click', '.inner-dhcp-box', function(event) { $(this).parents('#network').removeClass('inner-static-box-active'); });
    $('body').on('change', '.ble-data-format1,.ble-data-format2,.ble-data-format3,.ble-data-format4,.upgeade-tpe-box ', function(event) { });
});
