/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

var { Services } = ChromeUtils.import("resource://gre/modules/Services.jsm");
var { FileSource, L10nRegistry } = ChromeUtils.import("resource://gre/modules/L10nRegistry.jsm");

/**
 * Helper function to asynchronously call a certain method on the objects passed
 * in 'services' in order (i.e wait until the first completes before calling the
 * second
 *
 * @param method        The method name to call. Usually startup/shutdown.
 * @param services      The array of service objects to call on.
 */
function callOrderedServices(method, services) {
    let service = services.shift();
    if (service) {
        service[method]({
            onResult: function() {
                callOrderedServices(method, services);
            }
        });
    }
}

function calStartupService() {
    this.wrappedJSObject = this;
    this.setupObservers();
}

calStartupService.prototype = {
    QueryInterface: ChromeUtils.generateQI([Ci.nsIObserver]),
    classID: Components.ID("{2547331f-34c0-4a4b-b93c-b503538ba6d6}"),

    // Startup Service Methods

    /**
     * Sets up the needed observers for noticing startup/shutdown
     */
    setupObservers: function() {
        Services.obs.addObserver(this, "profile-after-change");
        Services.obs.addObserver(this, "profile-before-change");
        Services.obs.addObserver(this, "xpcom-shutdown");
    },

    started: false,

    /**
     * Gets the startup order of services. This is an array of service objects
     * that should be called in order at startup.
     *
     * @return      The startup order as an array.
     */
    getStartupOrder: function() {
        let self = this;
        let tzService = Cc["@mozilla.org/calendar/timezone-service;1"]
                          .getService(Ci.calITimezoneService)
                          .QueryInterface(Ci.calIStartupService);
        let calMgr = Cc["@mozilla.org/calendar/manager;1"]
                       .getService(Ci.calICalendarManager)
                       .QueryInterface(Ci.calIStartupService);

        // Localization service
        let locales = {
            startup: function(aCompleteListener) {
                let packaged = Services.locale.packagedLocales;
                let fileSrc = new FileSource(
                    "calendar", packaged,
                    "resource://calendar/chrome/calendar-{locale}/locale/{locale}/"
                );
                L10nRegistry.registerSource(fileSrc);
                aCompleteListener.onResult(null, Cr.NS_OK);
            },
            shutdown: function(aCompleteListener) {
                aCompleteListener.onResult(null, Cr.NS_OK);
            }
        };

        // Notification object
        let notify = {
            startup: function(aCompleteListener) {
                self.started = true;
                Services.obs.notifyObservers(null, "calendar-startup-done");
                aCompleteListener.onResult(null, Cr.NS_OK);
            },
            shutdown: function(aCompleteListener) {
                // Argh, it would have all been so pretty! Since we just reverse
                // the array, the shutdown notification would happen before the
                // other shutdown calls. For lack of pretty code, I'm
                // leaving this out! Users can still listen to xpcom-shutdown.
                self.started = false;
                aCompleteListener.onResult(null, Cr.NS_OK);
            }
        };

        // We need to spin up the timezone service before the calendar manager
        // to ensure we have the timezones initialized. Make sure "notify" is
        // last in this array!
        return [locales, tzService, calMgr, notify];
    },

    /**
     * Observer notification callback
     */
    observe: function(aSubject, aTopic, aData) {
        switch (aTopic) {
            case "profile-after-change":
                callOrderedServices("startup", this.getStartupOrder());
                break;
            case "profile-before-change":
                callOrderedServices("shutdown", this.getStartupOrder().reverse());
                break;
            case "xpcom-shutdown":
                Services.obs.removeObserver(this, "profile-after-change");
                Services.obs.removeObserver(this, "profile-before-change");
                Services.obs.removeObserver(this, "xpcom-shutdown");
                break;
        }
    }
};
