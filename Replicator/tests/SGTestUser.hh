//
// Created by Callum Birks on 10/11/2022.
//
/**
 * The TestUser class allows for easy creation of a temporary user within a test. This is especially useful when
 * isolating tests by channel, and there are functions in this class which make usage of channels easier.
 *
 * The constructor will create a user on SGW with the specified username and password and assign it to the
 * given channels. The destructor will automatically delete the user from SGW, so there is no need to manually
 * delete the user as this will be called when the object goes out of scope.
 *
 * To give a user access to all channels, pass `{"*"}` to the constructor as the channels
 *
 * Typical usage looks like:
 * ` const std::string channelID = "b"
 * ` TestUser testUser { _sg, "Bob", { channelID } }
 * ` _sg.authHeader = testUser.authHeader()
 * Where _sg is an SG object with specification for the SGW you are using
 * In any test suite inheriting from ReplicatorAPITest, there is already an _sg object set up for you.
 */

#ifndef LITECORE_SGTESTUSER_HH
#define LITECORE_SGTESTUSER_HH

#include "SG.hh"

class SG::TestUser {
public:
    explicit TestUser(SG& sg,
                      const std::string& username,
                      const std::vector<std::string>& channels = {},
                      const std::string& password = "password")
            : _sg(sg), _username(username), _password(password), _channels(channels)
    {
        REQUIRE(_sg.createUser(_username, _password, _channels));
        REQUIRE(_sg.assignUserChannel(_username, _channels));
        _authHeader = HTTPLogic::basicAuth(_username, _password);
    }

    ~TestUser() {
        _sg.deleteUser(_username);
    }

    alloc_slice authHeader() const { return _authHeader; }

    bool addChannels(const std::vector<std::string>& channels);
    bool setChannels(const std::vector<std::string>& channels);
    bool revokeAllChannels();

    const std::string _username;
    const std::string _password;

private:
    SG& _sg;
    alloc_slice _authHeader;
    std::vector<std::string> _channels;
};

#endif //LITECORE_SGTESTUSER_HH
