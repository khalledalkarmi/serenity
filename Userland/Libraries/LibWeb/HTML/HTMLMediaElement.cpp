/*
 * Copyright (c) 2020, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibVideo/Containers/Matroska/MatroskaDemuxer.h>
#include <LibWeb/Bindings/HTMLMediaElementPrototype.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/DOM/Event.h>
#include <LibWeb/Fetch/Fetching/Fetching.h>
#include <LibWeb/Fetch/Infrastructure/FetchAlgorithms.h>
#include <LibWeb/Fetch/Infrastructure/FetchController.h>
#include <LibWeb/Fetch/Infrastructure/HTTP/Requests.h>
#include <LibWeb/Fetch/Infrastructure/HTTP/Responses.h>
#include <LibWeb/HTML/CORSSettingAttribute.h>
#include <LibWeb/HTML/HTMLAudioElement.h>
#include <LibWeb/HTML/HTMLMediaElement.h>
#include <LibWeb/HTML/HTMLVideoElement.h>
#include <LibWeb/HTML/PotentialCORSRequest.h>
#include <LibWeb/HTML/TrackEvent.h>
#include <LibWeb/HTML/VideoTrack.h>
#include <LibWeb/HTML/VideoTrackList.h>
#include <LibWeb/MimeSniff/MimeType.h>

namespace Web::HTML {

HTMLMediaElement::HTMLMediaElement(DOM::Document& document, DOM::QualifiedName qualified_name)
    : HTMLElement(document, move(qualified_name))
{
}

HTMLMediaElement::~HTMLMediaElement() = default;

JS::ThrowCompletionOr<void> HTMLMediaElement::initialize(JS::Realm& realm)
{
    MUST_OR_THROW_OOM(Base::initialize(realm));
    set_prototype(&Bindings::ensure_web_prototype<Bindings::HTMLMediaElementPrototype>(realm, "HTMLMediaElement"));

    m_video_tracks = TRY(realm.heap().allocate<VideoTrackList>(realm, realm));

    return {};
}

// https://html.spec.whatwg.org/multipage/media.html#queue-a-media-element-task
void HTMLMediaElement::queue_a_media_element_task(JS::SafeFunction<void()> steps)
{
    // To queue a media element task with a media element element and a series of steps steps, queue an element task on the media element's
    // media element event task source given element and steps.
    queue_an_element_task(media_element_event_task_source(), move(steps));
}

void HTMLMediaElement::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_fetch_controller);
    visitor.visit(m_video_tracks);
}

void HTMLMediaElement::parse_attribute(DeprecatedFlyString const& name, DeprecatedString const& value)
{
    Base::parse_attribute(name, value);

    if (name == HTML::AttributeNames::src)
        load_element().release_value_but_fixme_should_propagate_errors();
}

// https://html.spec.whatwg.org/multipage/media.html#dom-navigator-canplaytype
WebIDL::ExceptionOr<Bindings::CanPlayTypeResult> HTMLMediaElement::can_play_type(DeprecatedString const& type) const
{
    auto& vm = this->vm();

    // The canPlayType(type) method must:
    // - return the empty string if type is a type that the user agent knows it cannot render or is the type "application/octet-stream"
    // - return "probably" if the user agent is confident that the type represents a media resource that it can render if used in with this audio or video element
    // - return "maybe" otherwise. Implementers are encouraged to return "maybe" unless the type can be confidently established as being supported or not
    // Generally, a user agent should never return "probably" for a type that allows the codecs parameter if that parameter is not present.
    if (type == "application/octet-stream"sv)
        return Bindings::CanPlayTypeResult::Empty;

    auto mime_type = TRY_OR_THROW_OOM(vm, MimeSniff::MimeType::parse(type));

    if (mime_type.has_value() && mime_type->type() == "video"sv) {
        if (mime_type->subtype() == "webm"sv)
            return Bindings::CanPlayTypeResult::Probably;
        return Bindings::CanPlayTypeResult::Maybe;
    }

    return Bindings::CanPlayTypeResult::Empty;
}

// https://html.spec.whatwg.org/multipage/media.html#dom-media-load
void HTMLMediaElement::load() const
{
    dbgln("(STUBBED) HTMLMediaElement::load()");
}

// https://html.spec.whatwg.org/multipage/media.html#dom-media-duration
double HTMLMediaElement::duration() const
{
    // The duration attribute must return the time of the end of the media resource, in seconds, on the media timeline. If no media data is available,
    // then the attributes must return the Not-a-Number (NaN) value. If the media resource is not known to be bounded (e.g. streaming radio, or a live
    // event with no announced end time), then the attribute must return the positive Infinity value.

    // FIXME: Handle unbounded media resources.
    return m_duration;
}

// https://html.spec.whatwg.org/multipage/media.html#durationChange
void HTMLMediaElement::set_duration(double duration)
{
    // When the length of the media resource changes to a known value (e.g. from being unknown to known, or from a previously established length to a new
    // length) the user agent must queue a media element task given the media element to fire an event named durationchange at the media element. (The event
    // is not fired when the duration is reset as part of loading a new media resource.) If the duration is changed such that the current playback position
    // ends up being greater than the time of the end of the media resource, then the user agent must also seek to the time of the end of the media resource.
    if (!isnan(duration)) {
        // FIXME: Handle seeking to the end of the media resource when needed.
        queue_a_media_element_task([this] {
            dispatch_event(DOM::Event::create(realm(), HTML::EventNames::durationchange.to_deprecated_fly_string()).release_value_but_fixme_should_propagate_errors());
        });
    }

    m_duration = duration;
}

// https://html.spec.whatwg.org/multipage/media.html#dom-media-pause
void HTMLMediaElement::pause() const
{
    dbgln("(STUBBED) HTMLMediaElement::pause()");
}

// https://html.spec.whatwg.org/multipage/media.html#media-element-load-algorithm
WebIDL::ExceptionOr<void> HTMLMediaElement::load_element()
{
    auto& vm = this->vm();

    m_first_data_load_event_since_load_start = true;

    // FIXME: 1. Abort any already-running instance of the resource selection algorithm for this element.

    // 2. Let pending tasks be a list of all tasks from the media element's media element event task source in one of the task queues.
    [[maybe_unused]] auto pending_tasks = TRY_OR_THROW_OOM(vm, HTML::main_thread_event_loop().task_queue().take_tasks_matching([&](auto& task) {
        return task.source() == media_element_event_task_source();
    }));

    // FIXME: 3. For each task in pending tasks that would resolve pending play promises or reject pending play promises, immediately resolve or
    //           reject those promises in the order the corresponding tasks were queued.

    // 4. Remove each task in pending tasks from its task queue
    //    NOTE: We performed this step along with step 2.

    // 5. If the media element's networkState is set to NETWORK_LOADING or NETWORK_IDLE, queue a media element task given the media element to
    //    fire an event named abort at the media element.
    if (m_network_state == NetworkState::Loading || m_network_state == NetworkState::Idle) {
        queue_a_media_element_task([this] {
            dispatch_event(DOM::Event::create(realm(), HTML::EventNames::abort.to_deprecated_fly_string()).release_value_but_fixme_should_propagate_errors());
        });
    }

    // 6. If the media element's networkState is not set to NETWORK_EMPTY, then:
    if (m_network_state != NetworkState::Empty) {
        // 1. Queue a media element task given the media element to fire an event named emptied at the media element.
        queue_a_media_element_task([this] {
            dispatch_event(DOM::Event::create(realm(), HTML::EventNames::emptied.to_deprecated_fly_string()).release_value_but_fixme_should_propagate_errors());
        });

        // 2. If a fetching process is in progress for the media element, the user agent should stop it.
        if (m_fetch_controller && m_fetch_controller->state() == Fetch::Infrastructure::FetchController::State::Ongoing)
            m_fetch_controller->terminate();

        // FIXME: 3. If the media element's assigned media provider object is a MediaSource object, then detach it.
        // FIXME: 4. Forget the media element's media-resource-specific tracks.

        // 5. If readyState is not set to HAVE_NOTHING, then set it to that state.
        if (m_ready_state != ReadyState::HaveNothing)
            set_ready_state(ReadyState::HaveNothing);

        // FIXME: 6. If the paused attribute is false, then:
        //            1. Set the paused attribute to true.
        //            2. Take pending play promises and reject pending play promises with the result and an "AbortError" DOMException.
        // FIXME: 7. If seeking is true, set it to false.
        // FIXME: 8. Set the current playback position to 0.
        //            Set the official playback position to 0.
        //            If this changed the official playback position, then queue a media element task given the media element to fire an
        //            event named timeupdate at the media element.
        // FIXME: 9. Set the timeline offset to Not-a-Number (NaN).

        // 10. Update the duration attribute to Not-a-Number (NaN).
        set_duration(NAN);
    }

    // FIXME: 7. Set the playbackRate attribute to the value of the defaultPlaybackRate attribute.
    // FIXME: 8. Set the error attribute to null and the can autoplay flag to true.

    // 9. Invoke the media element's resource selection algorithm.
    TRY(select_resource());

    // 10. NOTE: Playback of any previously playing media resource for this element stops.
    return {};
}

enum class SelectMode {
    Object,
    Attribute,
    Children,
};

// https://html.spec.whatwg.org/multipage/media.html#concept-media-load-algorithm
WebIDL::ExceptionOr<void> HTMLMediaElement::select_resource()
{
    // 1. Set the element's networkState attribute to the NETWORK_NO_SOURCE value.
    m_network_state = NetworkState::NoSource;

    // FIXME: 2. Set the element's show poster flag to true.
    // FIXME: 3. Set the media element's delaying-the-load-event flag to true (this delays the load event).

    // FIXME: 4. Await a stable state, allowing the task that invoked this algorithm to continue. The synchronous section consists of all the remaining
    //           steps of this algorithm until the algorithm says the synchronous section has ended. (Steps in synchronous sections are marked with ⌛.)

    // FIXME: 5. ⌛ If the media element's blocked-on-parser flag is false, then populate the list of pending text tracks.

    auto mode = SelectMode::Children;

    // 6. FIXME: ⌛ If the media element has an assigned media provider object, then let mode be object.
    //
    //    ⌛ Otherwise, if the media element has no assigned media provider object but has a src attribute, then let mode be attribute.
    if (has_attribute(HTML::AttributeNames::src)) {
        mode = SelectMode::Attribute;
    }
    //    FIXME: ⌛ Otherwise, if the media element does not have an assigned media provider object and does not have a src attribute, but does have
    //            a source element child, then let mode be children and let candidate be the first such source element child in tree order.
    //
    //    ⌛ Otherwise the media element has no assigned media provider object and has neither a src attribute nor a source element child:
    else {
        // 1. ⌛ Set the networkState to NETWORK_EMPTY.
        m_network_state = NetworkState::Empty;

        // FIXME: 2. ⌛ Set the element's delaying-the-load-event flag to false. This stops delaying the load event.

        // 3. End the synchronous section and return.
        return {};
    }

    // 7. ⌛ Set the media element's networkState to NETWORK_LOADING.
    m_network_state = NetworkState::Loading;

    // 8. ⌛ Queue a media element task given the media element to fire an event named loadstart at the media element.
    queue_a_media_element_task([this] {
        dispatch_event(DOM::Event::create(realm(), HTML::EventNames::loadstart.to_deprecated_fly_string()).release_value_but_fixme_should_propagate_errors());
    });

    // 9. Run the appropriate steps from the following list:
    switch (mode) {
    // -> If mode is object
    case SelectMode::Object:
        // FIXME: 1. ⌛ Set the currentSrc attribute to the empty string.
        // FIXME: 2. End the synchronous section, continuing the remaining steps in parallel.
        // FIXME: 3. Run the resource fetch algorithm with the assigned media provider object. If that algorithm returns without aborting this one,
        //           then theload failed.
        // FIXME: 4. Failed with media provider: Reaching this step indicates that the media resource failed to load. Take pending play promises and queue
        //           a media element task given the media element to run the dedicated media source failure steps with the result.
        // FIXME: 5. Wait for the task queued by the previous step to have executed.

        // 6. Return. The element won't attempt to load another resource until this algorithm is triggered again.
        return {};

    // -> If mode is attribute
    case SelectMode::Attribute: {
        auto failed_with_attribute = [this]() {
            bool ran_media_element_task = false;

            // 6. Failed with attribute: Reaching this step indicates that the media resource failed to load or that the given URL could not be parsed. Take
            //    pending play promises and queue a media element task given the media element to run the dedicated media source failure steps with the result.
            queue_a_media_element_task([this, &ran_media_element_task]() {
                // FIXME: Find and pass pending play promises to this AO.
                handle_media_source_failure().release_value_but_fixme_should_propagate_errors();

                ran_media_element_task = true;
            });

            // 7. Wait for the task queued by the previous step to have executed.
            HTML::main_thread_event_loop().spin_until([&]() { return ran_media_element_task; });
        };

        // 1. ⌛ If the src attribute's value is the empty string, then end the synchronous section, and jump down to the failed with attribute step below.
        auto source = attribute(HTML::AttributeNames::src);
        if (source.is_empty()) {
            failed_with_attribute();
            return {};
        }

        // 2. ⌛ Let urlString and urlRecord be the resulting URL string and the resulting URL record, respectively, that would have resulted from parsing
        //    the URL specified by the src attribute's value relative to the media element's node document when the src attribute was last changed.
        auto url_record = document().parse_url(source);

        // FIXME: 3. ⌛ If urlString was obtained successfully, set the currentSrc attribute to urlString.

        // 4. End the synchronous section, continuing the remaining steps in parallel.

        // 5. If urlRecord was obtained successfully, run the resource fetch algorithm with urlRecord. If that algorithm returns without aborting this one,
        //    then the load failed.
        if (url_record.is_valid()) {
            TRY(fetch_resource(url_record, move(failed_with_attribute)));
            return {};
        }

        failed_with_attribute();

        // 8. Return. The element won't attempt to load another resource until this algorithm is triggered again.
        return {};
    }

    // -> Otherwise (mode is children)
    case SelectMode::Children:
        // FIXME: 1. ⌛ Let pointer be a position defined by two adjacent nodes in the media element's child list, treating the start of the list (before the
        //           first child in the list, if any) and end of the list (after the last child in the list, if any) as nodes in their own right. One node is
        //           the node before pointer, and the other node is the node after pointer. Initially, let pointer be the position between the candidate node
        //           and the next node, if there are any, or the end of the list, if it is the last node.
        //
        //           As nodes are inserted and removed into the media element, pointer must be updated as follows:
        //
        //           If a new node is inserted between the two nodes that define pointer
        //               Let pointer be the point between the node before pointer and the new node. In other words, insertions at pointer go after pointer.
        //           If the node before pointer is removed
        //               Let pointer be the point between the node after pointer and the node before the node after pointer. In other words, pointer doesn't
        //               move relative to the remaining nodes.
        //           If the node after pointer is removed
        //               Let pointer be the point between the node before pointer and the node after the node before pointer. Just as with the previous case,
        //               pointer doesn't move relative to the remaining nodes.
        //           Other changes don't affect pointer.

        // FIXME: 2. ⌛ Process candidate: If candidate does not have a src attribute, or if its src attribute's value is the empty string, then end the
        //           synchronous section, and jump down to the failed with elements step below.
        // FIXME: 3. ⌛ Let urlString and urlRecord be the resulting URL string and the resulting URL record, respectively, that would have resulted from parsing
        //           the URL specified by candidate's src attribute's value relative to the candidate's node document when the src attribute was last changed.
        // FIXME: 4. ⌛ If urlString was not obtained successfully, then end the synchronous section, and jump down to the failed with elements step below.
        // FIXME: 5. ⌛ If candidate has a type attribute whose value, when parsed as a MIME type (including any codecs described by the codecs parameter, for
        //           types that define that parameter), represents a type that the user agent knows it cannot render, then end the synchronous section, and
        //           jump down to the failed with elements step below.
        // FIXME: 6. ⌛ Set the currentSrc attribute to urlString.
        // FIXME: 7. End the synchronous section, continuing the remaining steps in parallel.
        // FIXME: 8. Run the resource fetch algorithm with urlRecord. If that algorithm returns without aborting this one, then the load failed.
        // FIXME: 9. Failed with elements: Queue a media element task given the media element to fire an event named error at candidate.
        // FIXME: 10. Await a stable state. The synchronous section consists of all the remaining steps of this algorithm until the algorithm says the
        //            synchronous section has ended. (Steps in synchronous sections are marked with ⌛.)
        // FIXME: 11. ⌛ Forget the media element's media-resource-specific tracks.
        // FIXME: 12. ⌛ Find next candidate: Let candidate be null.
        // FIXME: 13. ⌛ Search loop: If the node after pointer is the end of the list, then jump to the waiting step below.
        // FIXME: 14. ⌛ If the node after pointer is a source element, let candidate be that element.
        // FIXME: 15. ⌛ Advance pointer so that the node before pointer is now the node that was after pointer, and the node after pointer is the node after
        //            the node that used to be after pointer, if any.
        // FIXME: 16. ⌛ If candidate is null, jump back to the search loop step. Otherwise, jump back to the process candidate step.
        // FIXME: 17. ⌛ Waiting: Set the element's networkState attribute to the NETWORK_NO_SOURCE value.
        // FIXME: 18. ⌛ Set the element's show poster flag to true.
        // FIXME: 19. ⌛ Queue a media element task given the media element to set the element's delaying-the-load-event flag to false. This stops delaying the
        //            load event.
        // FIXME: 20. End the synchronous section, continuing the remaining steps in parallel.
        // FIXME: 21. Wait until the node after pointer is a node other than the end of the list. (This step might wait forever.)
        // FIXME: 22. Await a stable state. The synchronous section consists of all the remaining steps of this algorithm until the algorithm says the
        //            synchronous section has ended. (Steps in synchronous sections are marked with ⌛.)
        // FIXME: 23. ⌛ Set the element's delaying-the-load-event flag back to true (this delays the load event again, in case it hasn't been fired yet).
        // FIXME: 24. ⌛ Set the networkState back to NETWORK_LOADING.
        // FIXME: 25. ⌛ Jump back to the find next candidate step above.
        break;
    }

    return {};
}

enum class FetchMode {
    Local,
    Remote,
};

// https://html.spec.whatwg.org/multipage/media.html#concept-media-load-resource
WebIDL::ExceptionOr<void> HTMLMediaElement::fetch_resource(AK::URL const& url_record, Function<void()> failure_callback)
{
    auto& realm = this->realm();
    auto& vm = realm.vm();

    // 1. If the algorithm was invoked with media provider object or a URL record whose blob URL entry is a blob URL entry whose object is a media provider
    //    object, then let mode be local. Otherwise let mode be remote.
    // FIXME: Detect media provider object / blob URLs with a media provider object.
    auto mode = FetchMode::Remote;

    // FIXME: 2. If mode is remote, then let the current media resource be the resource given by the URL record passed to this algorithm; otherwise, let the
    //           current media resource be the resource given by the media provider object. Either way, the current media resource is now the element's media
    //           resource.
    // FIXME: 3. Remove all media-resource-specific text tracks from the media element's list of pending text tracks, if any.

    // 4. Run the appropriate steps from the following list:
    switch (mode) {
    // -> If mode is remote
    case FetchMode::Remote: {
        // FIXME: 1. Optionally, run the following substeps. This is the expected behavior if the user agent intends to not attempt to fetch the resource until
        //           the user requests it explicitly (e.g. as a way to implement the preload attribute's none keyword).
        //            1. Set the networkState to NETWORK_IDLE.
        //            2. Queue a media element task given the media element to fire an event named suspend at the element.
        //            3. Queue a media element task given the media element to set the element's delaying-the-load-event flag to false. This stops delaying
        //               the load event.
        //            4. Wait for the task to be run.
        //            5. Wait for an implementation-defined event (e.g., the user requesting that the media element begin playback).
        //            6. Set the element's delaying-the-load-event flag back to true (this delays the load event again, in case it hasn't been fired yet).
        //            7. Set the networkState to NETWORK_LOADING.

        // 2. Let destination be "audio" if the media element is an audio element, or "video" otherwise.
        auto destination = is<HTMLAudioElement>(*this)
            ? Fetch::Infrastructure::Request::Destination::Audio
            : Fetch::Infrastructure::Request::Destination::Video;

        // 3. Let request be the result of creating a potential-CORS request given current media resource's URL record, destination, and the current state
        //    of media element's crossorigin content attribute.
        // FIXME: Parse the media element's crossorigin content attribute.
        auto request = create_potential_CORS_request(vm, url_record, destination, CORSSettingAttribute::Anonymous);

        // 4. Set request's client to the media element's node document's relevant settings object.
        request->set_client(&document().relevant_settings_object());

        // 5. Set request's initiator type to destination.
        request->set_initiator_type(destination == Fetch::Infrastructure::Request::Destination::Audio
                ? Fetch::Infrastructure::Request::InitiatorType::Audio
                : Fetch::Infrastructure::Request::InitiatorType::Video);

        // 6. Let byteRange, which is "entire resource" or a (number, number or "until end") tuple, be the byte range required to satisfy missing data in
        //    media data. This value is implementation-defined and may rely on codec, network conditions or other heuristics. The user-agent may determine
        //    to fetch the resource in full, in which case byteRange would be "entire resource", to fetch from a byte offset until the end, in which case
        //    byteRange would be (number, "until end"), or to fetch a range between two byte offsets, im which case byteRange would be a (number, number)
        //    tuple representing the two offsets.
        ByteRange byte_range = EntireResource {};

        // FIXME: 7. If byteRange is not "entire resource", then:
        //            1. If byteRange[1] is "until end" then add a range header to request given byteRange[0].
        //            2. Otherwise, add a range header to request given byteRange[0] and byteRange[1].

        // 8. Fetch request, with processResponse set to the following steps given response response:
        Fetch::Infrastructure::FetchAlgorithms::Input fetch_algorithms_input {};

        fetch_algorithms_input.process_response = [this, byte_range = move(byte_range), failure_callback = move(failure_callback)](auto response) mutable {
            auto& realm = this->realm();

            // 1. Let global be the media element's node document's relevant global object.
            auto& global = document().realm().global_object();

            // 4. If the result of verifying response given the current media resource and byteRange is false, then abort these steps.
            // NOTE: We do this step before creating the updateMedia task so that we can invoke the failure callback.
            if (!verify_response(response, byte_range)) {
                failure_callback();
                return;
            }

            // 2. Let updateMedia be to queue a media element task given the media element to run the first appropriate steps from the media data processing
            //    steps list below. (A new task is used for this so that the work described below occurs relative to the appropriate media element event task
            //    source rather than using the networking task source.)
            auto update_media = [this, failure_callback = move(failure_callback)](auto media_data) mutable {
                // 6. Update the media data with the contents of response's unsafe response obtained in this fashion. response can be CORS-same-origin or
                //    CORS-cross-origin; this affects whether subtitles referenced in the media data are exposed in the API and, for video elements, whether
                //    a canvas gets tainted when the video is drawn on it.
                m_media_data = move(media_data);

                queue_a_media_element_task([this, failure_callback = move(failure_callback)]() mutable {
                    process_media_data(move(failure_callback)).release_value_but_fixme_should_propagate_errors();
                });
            };

            // FIXME: 3. Let processEndOfMedia be the following step: If the fetching process has completes without errors, including decoding the media data,
            //           and if all of the data is available to the user agent without network access, then, the user agent must move on to the final step below.
            //           This might never happen, e.g. when streaming an infinite resource such as web radio, or if the resource is longer than the user agent's
            //           ability to cache data.

            // 5. Otherwise, incrementally read response's body given updateMedia, processEndOfMedia, an empty algorithm, and global.
            VERIFY(response->body().has_value());
            auto empty_algorithm = [](auto&) {};

            // FIXME: We are "fully" reading the response here, rather than "incrementally". Memory concerns aside, this should be okay for now as we are
            //        always setting byteRange to "entire resource". However, we should switch to incremental reads when that is implemented, and then
            //        implement the processEndOfMedia step.
            response->body()->fully_read(realm, move(update_media), move(empty_algorithm), JS::NonnullGCPtr { global }).release_value_but_fixme_should_propagate_errors();
        };

        m_fetch_controller = TRY(Fetch::Fetching::fetch(realm, request, Fetch::Infrastructure::FetchAlgorithms::create(vm, move(fetch_algorithms_input))));
        break;
    }

    // -> Otherwise (mode is local)
    case FetchMode::Local:
        // FIXME:
        // The resource described by the current media resource, if any, contains the media data. It is CORS-same-origin.
        //
        // If the current media resource is a raw data stream (e.g. from a File object), then to determine the format of the media resource, the user agent
        // must use the rules for sniffing audio and video specifically. Otherwise, if the data stream is pre-decoded, then the format is the format given
        // by the relevant specification.
        //
        // Whenever new data for the current media resource becomes available, queue a media element task given the media element to run the first appropriate
        // steps from the media data processing steps list below.
        //
        // When the current media resource is permanently exhausted (e.g. all the bytes of a Blob have been processed), if there were no decoding errors,
        // then the user agent must move on to the final step below. This might never happen, e.g. if the current media resource is a MediaStream.
        break;
    }

    return {};
}

// https://html.spec.whatwg.org/multipage/media.html#verify-a-media-response
bool HTMLMediaElement::verify_response(JS::NonnullGCPtr<Fetch::Infrastructure::Response> response, ByteRange const& byte_range)
{
    // 1. If response is a network error, then return false.
    if (response->is_network_error())
        return false;

    // 2. If byteRange is "entire resource", then return true.
    if (byte_range.has<EntireResource>())
        return true;

    // 3. Let internalResponse be response's unsafe response.
    // 4. If internalResponse's status is 200, then return true.
    // 5. If internalResponse's status is not 206, then return false.
    // 6. If the result of extracting content-range values from internalResponse is failure, then return false.
    TODO();
}

// https://html.spec.whatwg.org/multipage/media.html#media-data-processing-steps-list
WebIDL::ExceptionOr<void> HTMLMediaElement::process_media_data(Function<void()> failure_callback)
{
    auto& realm = this->realm();
    auto& vm = realm.vm();

    // -> If the media data cannot be fetched at all, due to network errors, causing the user agent to give up trying to fetch the resource
    // -> If the media data can be fetched but is found by inspection to be in an unsupported format, or can otherwise not be rendered at all
    auto demuxer = Video::Matroska::MatroskaDemuxer::from_data(m_media_data);
    if (demuxer.is_error()) {
        // 1. The user agent should cancel the fetching process.
        m_fetch_controller->terminate();

        // 2. Abort this subalgorithm, returning to the resource selection algorithm.
        failure_callback();

        return {};
    }

    JS::GCPtr<VideoTrack> video_track;

    // -> If the media resource is found to have an audio track
    if (auto audio_tracks = demuxer.value()->get_tracks_for_type(Video::TrackType::Audio); !audio_tracks.is_error() && !audio_tracks.value().is_empty()) {
        // FIXME: 1. Create an AudioTrack object to represent the audio track.
        // FIXME: 2. Update the media element's audioTracks attribute's AudioTrackList object with the new AudioTrack object.
        // FIXME: 3. Let enable be unknown.
        // FIXME: 4. If either the media resource or the URL of the current media resource indicate a particular set of audio tracks to enable, or if
        //           the user agent has information that would facilitate the selection of specific audio tracks to improve the user's experience, then:
        //           if this audio track is one of the ones to enable, then set enable to true, otherwise, set enable to false.
        // FIXME: 5. If enable is still unknown, then, if the media element does not yet have an enabled audio track, then set enable to true, otherwise,
        //           set enable to false.
        // FIXME: 6. If enable is true, then enable this audio track, otherwise, do not enable this audio track.
        // FIXME: 7. Fire an event named addtrack at this AudioTrackList object, using TrackEvent, with the track attribute initialized to the new AudioTrack object.
    }

    // -> If the media resource is found to have a video track
    if (auto video_tracks = demuxer.value()->get_tracks_for_type(Video::TrackType::Video); !video_tracks.is_error() && !video_tracks.value().is_empty()) {
        // 1. Create a VideoTrack object to represent the video track.
        video_track = TRY(vm.heap().allocate<VideoTrack>(realm, realm, *this, demuxer.release_value(), video_tracks.value()[0]));

        // 2. Update the media element's videoTracks attribute's VideoTrackList object with the new VideoTrack object.
        TRY_OR_THROW_OOM(vm, m_video_tracks->add_track({}, *video_track));

        // 3. Let enable be unknown.
        auto enable = TriState::Unknown;

        // FIXME: 4. If either the media resource or the URL of the current media resource indicate a particular set of video tracks to enable, or if
        //           the user agent has information that would facilitate the selection of specific video tracks to improve the user's experience, then:
        //           if this video track is the first such video track, then set enable to true, otherwise, set enable to false.

        // 5. If enable is still unknown, then, if the media element does not yet have a selected video track, then set enable to true, otherwise, set
        //    enable to false.
        if (enable == TriState::Unknown)
            enable = m_video_tracks->selected_index() == -1 ? TriState::True : TriState::False;

        // 6. If enable is true, then select this track and unselect any previously selected video tracks, otherwise, do not select this video track.
        //    If other tracks are unselected, then a change event will be fired.
        if (enable == TriState::True)
            video_track->set_selected(true);

        // 7. Fire an event named addtrack at this VideoTrackList object, using TrackEvent, with the track attribute initialized to the new VideoTrack object.
        TrackEventInit event_init {};
        event_init.track = video_track;

        auto event = TRY(TrackEvent::create(realm, TRY_OR_THROW_OOM(vm, FlyString::from_deprecated_fly_string(HTML::EventNames::addtrack.to_deprecated_fly_string())), event_init));
        m_video_tracks->dispatch_event(event);
    }

    // -> Once enough of the media data has been fetched to determine the duration of the media resource, its dimensions, and other metadata
    if (video_track != nullptr) {
        // FIXME: 1. Establish the media timeline for the purposes of the current playback position and the earliest possible position, based on the media data.
        // FIXME: 2. Update the timeline offset to the date and time that corresponds to the zero time in the media timeline established in the previous step,
        //           if any. If no explicit time and date is given by the media resource, the timeline offset must be set to Not-a-Number (NaN).
        // FIXME: 3. Set the current playback position and the official playback position to the earliest possible position.

        // 4. Update the duration attribute with the time of the last frame of the resource, if known, on the media timeline established above. If it is
        //    not known (e.g. a stream that is in principle infinite), update the duration attribute to the value positive Infinity.
        // FIXME: Handle unbounded media resources.
        set_duration(static_cast<double>(video_track->duration().to_seconds()));

        // 5. For video elements, set the videoWidth and videoHeight attributes, and queue a media element task given the media element to fire an event
        //    named resize at the media element.
        if (is<HTMLVideoElement>(*this)) {
            auto& video_element = verify_cast<HTMLVideoElement>(*this);
            video_element.set_video_width(video_track->pixel_width());
            video_element.set_video_height(video_track->pixel_height());

            queue_a_media_element_task([this] {
                dispatch_event(DOM::Event::create(this->realm(), HTML::EventNames::resize.to_deprecated_fly_string()).release_value_but_fixme_should_propagate_errors());
            });
        }

        // 6. Set the readyState attribute to HAVE_METADATA.
        set_ready_state(ReadyState::HaveMetadata);

        // FIXME: 7. Let jumped be false.
        // FIXME: 8. If the media element's default playback start position is greater than zero, then seek to that time, and let jumped be true.
        // FIXME: 9. Let the media element's default playback start position be zero.
        // FIXME: 10. Let the initial playback position be zero.
        // FIXME: 11. If either the media resource or the URL of the current media resource indicate a particular start time, then set the initial playback
        //            position to that time and, if jumped is still false, seek to that time.
        // FIXME: 12. If there is no enabled audio track, then enable an audio track. This will cause a change event to be fired.

        // 13. If there is no selected video track, then select a video track. This will cause a change event to be fired.
        if (m_video_tracks->selected_index() == -1)
            video_track->set_selected(true);

        // FIXME: Once the readyState attribute reaches HAVE_CURRENT_DATA, after the loadeddata event has been fired, set the element's delaying-the-load-event
        //        flag to false. This stops delaying the load event.
    }

    // -> Once the entire media resource has been fetched (but potentially before any of it has been decoded)
    if (video_track != nullptr) {
        // Fire an event named progress at the media element.
        dispatch_event(TRY(DOM::Event::create(this->realm(), HTML::EventNames::progress.to_deprecated_fly_string())));

        // Set the networkState to NETWORK_IDLE and fire an event named suspend at the media element.
        m_network_state = NetworkState::Idle;
        dispatch_event(TRY(DOM::Event::create(this->realm(), HTML::EventNames::suspend.to_deprecated_fly_string())));

        // If the user agent ever discards any media data and then needs to resume the network activity to obtain it again, then it must queue a media
        // element task given the media element to set the networkState to NETWORK_LOADING.
    }

    // FIXME: -> If the connection is interrupted after some media data has been received, causing the user agent to give up trying to fetch the resource
    // FIXME: -> If the media data is corrupted
    // FIXME: -> If the media data fetching process is aborted by the user
    // FIXME: -> If the media data can be fetched but has non-fatal errors or uses, in part, codecs that are unsupported, preventing the user agent from
    //           rendering the content completely correctly but not preventing playback altogether
    // FIXME: -> If the media resource is found to declare a media-resource-specific text track that the user agent supports

    return {};
}

// https://html.spec.whatwg.org/multipage/media.html#dedicated-media-source-failure-steps
WebIDL::ExceptionOr<void> HTMLMediaElement::handle_media_source_failure()
{
    // FIXME: 1. Set the error attribute to the result of creating a MediaError with MEDIA_ERR_SRC_NOT_SUPPORTED.

    // 2. Forget the media element's media-resource-specific tracks.
    forget_media_resource_specific_tracks();

    // 3. Set the element's networkState attribute to the NETWORK_NO_SOURCE value.
    m_network_state = NetworkState::NoSource;

    // FIXME: 4. Set the element's show poster flag to true.

    // 5. Fire an event named error at the media element.
    dispatch_event(TRY(DOM::Event::create(realm(), HTML::EventNames::error.to_deprecated_fly_string())));

    // FIXME: 6. Reject pending play promises with promises and a "NotSupportedError" DOMException.
    // FIXME: 7. Set the element's delaying-the-load-event flag to false. This stops delaying the load event.

    return {};
}

// https://html.spec.whatwg.org/multipage/media.html#forget-the-media-element's-media-resource-specific-tracks
void HTMLMediaElement::forget_media_resource_specific_tracks()
{
    // When a media element is to forget the media element's media-resource-specific tracks, the user agent must remove from the media element's list
    // of text tracks all the media-resource-specific text tracks, then empty the media element's audioTracks attribute's AudioTrackList object, then
    // empty the media element's videoTracks attribute's VideoTrackList object. No events (in particular, no removetrack events) are fired as part of
    // this; the error and emptied events, fired by the algorithms that invoke this one, can be used instead.
    m_video_tracks->remove_all_tracks({});
}

// https://html.spec.whatwg.org/multipage/media.html#ready-states:media-element-3
void HTMLMediaElement::set_ready_state(ReadyState ready_state)
{
    ScopeGuard guard { [&] { m_ready_state = ready_state; } };

    // -> If the previous ready state was HAVE_NOTHING, and the new ready state is HAVE_METADATA
    if (m_ready_state == ReadyState::HaveNothing && ready_state == ReadyState::HaveMetadata) {
        // Queue a media element task given the media element to fire an event named loadedmetadata at the element.
        queue_a_media_element_task([this] {
            dispatch_event(DOM::Event::create(this->realm(), HTML::EventNames::loadedmetadata.to_deprecated_fly_string()).release_value_but_fixme_should_propagate_errors());
        });

        return;
    }

    // -> If the previous ready state was HAVE_METADATA and the new ready state is HAVE_CURRENT_DATA or greater
    if (m_ready_state == ReadyState::HaveMetadata && ready_state >= ReadyState::HaveCurrentData) {
        // If this is the first time this occurs for this media element since the load() algorithm was last invoked, the user agent must queue a media
        // element task given the media element to fire an event named loadeddata at the element.
        if (m_first_data_load_event_since_load_start) {
            m_first_data_load_event_since_load_start = false;

            queue_a_media_element_task([this] {
                dispatch_event(DOM::Event::create(this->realm(), HTML::EventNames::loadeddata.to_deprecated_fly_string()).release_value_but_fixme_should_propagate_errors());
            });
        }

        // If the new ready state is HAVE_FUTURE_DATA or HAVE_ENOUGH_DATA, then the relevant steps below must then be run also.
        if (ready_state != ReadyState::HaveFutureData && ready_state != ReadyState::HaveEnoughData)
            return;
    }

    // -> If the previous ready state was HAVE_FUTURE_DATA or more, and the new ready state is HAVE_CURRENT_DATA or less
    if (m_ready_state >= ReadyState::HaveFutureData && ready_state <= ReadyState::HaveCurrentData) {
        // FIXME: If the media element was potentially playing before its readyState attribute changed to a value lower than HAVE_FUTURE_DATA, and the element
        //        has not ended playback, and playback has not stopped due to errors, paused for user interaction, or paused for in-band content, the user agent
        //        must queue a media element task given the media element to fire an event named timeupdate at the element, and queue a media element task given
        //        the media element to fire an event named waiting at the element.
        return;
    }

    // -> If the previous ready state was HAVE_CURRENT_DATA or less, and the new ready state is HAVE_FUTURE_DATA
    if (m_ready_state <= ReadyState::HaveCurrentData && ready_state == ReadyState::HaveFutureData) {
        // The user agent must queue a media element task given the media element to fire an event named canplay at the element.
        queue_a_media_element_task([this] {
            dispatch_event(DOM::Event::create(this->realm(), HTML::EventNames::canplay.to_deprecated_fly_string()).release_value_but_fixme_should_propagate_errors());
        });

        // FIXME: If the element's paused attribute is false, the user agent must notify about playing for the element.
        return;
    }

    // -> If the new ready state is HAVE_ENOUGH_DATA
    if (ready_state == ReadyState::HaveEnoughData) {
        // If the previous ready state was HAVE_CURRENT_DATA or less, the user agent must queue a media element task given the media element to fire an event
        // named canplay at the element, and, if the element's paused attribute is false, notify about playing for the element.
        if (m_ready_state <= ReadyState::HaveCurrentData) {
            // FIXME: Handle the paused attribute.
            queue_a_media_element_task([this] {
                dispatch_event(DOM::Event::create(this->realm(), HTML::EventNames::canplay.to_deprecated_fly_string()).release_value_but_fixme_should_propagate_errors());
            });
        }

        // The user agent must queue a media element task given the media element to fire an event named canplaythrough at the element.
        queue_a_media_element_task([this] {
            dispatch_event(DOM::Event::create(this->realm(), HTML::EventNames::canplaythrough.to_deprecated_fly_string()).release_value_but_fixme_should_propagate_errors());
        });

        // FIXME: If the element is not eligible for autoplay, then the user agent must abort these substeps.

        // FIXME: The user agent may run the following substeps:
        //            Set the paused attribute to false.
        //            If the element's show poster flag is true, set it to false and run the time marches on steps.
        //            Queue a media element task given the element to fire an event named play at the element.
        //            Notify about playing for the element.

        // FIXME: Alternatively, if the element is a video element, the user agent may start observing whether the element intersects the viewport. When the
        //        element starts intersecting the viewport, if the element is still eligible for autoplay, run the substeps above. Optionally, when the element
        //        stops intersecting the viewport, if the can autoplay flag is still true and the autoplay attribute is still specified, run the following substeps:
        //            Run the internal pause steps and set the can autoplay flag to true.
        //            Queue a media element task given the element to fire an event named pause at the element.
        return;
    }
}

}
